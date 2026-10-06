"""Refuse misleading native acceptance transcripts before connecting a console."""
import importlib.util
from pathlib import Path
import unittest
from unittest import mock
import hashlib
import json
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "publication_acceptance_runner", ROOT / "tools/run_publication_acceptance.py")
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)


class TranscriptTests(unittest.TestCase):
    nonce = "0123456789abcdef"

    def test_busy_writer_requires_actual_busy_errno(self):
        event = runner.parse_record(
            f"PWPA1 run={self.nonce} mode=writer-try stage=result pid=7 pass=1 rc=-1 errno=35",
            self.nonce, "writer-try")
        runner.require_writer(event, busy=True)
        for error in (0, 1, 2, 38):
            with self.subTest(error=error), self.assertRaises(ValueError):
                runner.require_writer({**event, "errno": str(error)}, busy=True)

    def test_foreign_or_ambiguous_records_refused(self):
        for line in (
            "PWPA1 run=ffffffffffffffff mode=reader-try stage=result pid=7 rc=0",
            f"PWPA1 run={self.nonce} mode=writer-try stage=result pid=7 rc=0",
            f"PWPA1 run={self.nonce} mode=reader-try stage=result pid=7 rc=0 rc=-1",
            f"PWPA1 run={self.nonce} mode=reader-try stage=result pid=0 rc=0",
        ):
            with self.subTest(line=line), self.assertRaises(ValueError):
                runner.parse_record(line, self.nonce, "reader-try")

    def test_pending_refusal_is_distinct_from_lock_contention(self):
        for actual in ("0", "-2"):
            with self.subTest(actual=actual), self.assertRaises(ValueError):
                runner.require_reader({"mode": "reader-try", "stage": "result", "pass": "1", "rc": actual}, expected=-1)
        runner.require_reader({"mode": "reader-try", "stage": "result", "pass": "1", "rc": "-1", "errstage": "1"}, expected=-1)

    def test_pending_refusal_requires_receipt_stage(self):
        for stage in ("0", "2", "7", None):
            event = {"mode": "reader-try", "stage": "result", "pass": "1", "rc": "-1"}
            if stage is not None:
                event["errstage"] = stage
            with self.subTest(stage=stage), self.assertRaises(ValueError):
                runner.require_reader(event, -1)

    def test_explicit_failure_and_contradictory_status_are_refused(self):
        for event in (
            {"mode": "writer-try", "stage": "result", "pass": "0", "rc": "0", "errno": "0"},
            {"mode": "writer-try", "stage": "result", "pass": "1", "rc": "0", "errno": "35"},
            {"mode": "writer-try", "stage": "result", "rc": "0", "errno": "0"},
        ):
            with self.subTest(event=event), self.assertRaises(ValueError):
                runner.require_writer(event, busy=False)
        with self.assertRaises(ValueError):
            runner.require_reader({"mode": "reader-try", "stage": "result", "pass": "0", "rc": "-1"}, -1)

    def test_pid_aliases_are_refused(self):
        for pid in ("007", "0007", "７"):
            with self.subTest(pid=pid), self.assertRaises(ValueError):
                runner.parse_record(f"PWPA1 run={self.nonce} mode=reader-try stage=result pid={pid} rc=0",
                                    self.nonce, "reader-try")

    def test_socket_setup_failure_closes_connected_socket(self):
        connection = mock.Mock()
        connection.settimeout.side_effect = OSError("setup failed")
        with mock.patch.object(runner.socket, "create_connection", return_value=connection):
            with self.assertRaises(OSError):
                runner.Payload("unused", 9305, b"elf", self.nonce, "init", [])
        connection.close.assert_called_once()

    def test_reviewed_manifest_rejects_stale_authored_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inputs = {}
            for name in ("probes/publication-acceptance/main.c", "src/platform/ps5/file_shims.c"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"reviewed source")
                inputs[name] = hashlib.sha256(path.read_bytes()).hexdigest()
            sdk_relative = "target/lib/test.a"
            sdk_file = root / ".deps/native/ps5-payload-sdk" / sdk_relative
            sdk_file.parent.mkdir(parents=True)
            sdk_file.write_bytes(b"sdk")
            inventory = {sdk_relative: hashlib.sha256(b"sdk").hexdigest()}
            manifest = {
                "schema": "playden-publication-acceptance/1",
                "prospero_revision": runner.REVISION, "foundation_revision": runner.FOUNDATION,
                "publication_provider_sha256": runner.PROVIDER,
                "elf": "publication-acceptance.elf", "elf_sha256": hashlib.sha256(b"elf").hexdigest(),
                "inputs": inputs, "sdk_inputs": inventory,
                "sdk_inputs_sha256": hashlib.sha256(json.dumps(inventory, sort_keys=True, separators=(",", ":")).encode()).hexdigest(),
                "acceptance_scope": "standalone-payload; no granted-wrapper lifetime claim",
            }
            manifest_path = root / "manifest.json"
            manifest_path.write_text(json.dumps(manifest))
            (root / "publication-acceptance.elf").write_bytes(b"elf")
            pin = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
            runner.validate_manifest(manifest_path, pin, root, root)
            (root / "probes/publication-acceptance/main.c").write_bytes(b"new unbuilt source")
            with self.assertRaisesRegex(ValueError, "stale"):
                runner.validate_manifest(manifest_path, pin, root, root)
            with self.assertRaisesRegex(ValueError, "reviewed digest"):
                runner.validate_manifest(manifest_path, "0" * 64, root, root)

    def test_failed_holder_is_stopped_before_cleanup_and_busy_release_preserves_fixture(self):
        for release_busy in (False, True):
            with self.subTest(release_busy=release_busy):
                trace = []
                writer_count = [0]
                class FakePayload:
                    def __init__(self, host, port, elf, nonce, mode, transcript):
                        self.mode = mode
                        trace.append("start:" + mode)
                    def await_stage(self, stage):
                        base = {"stage": stage, "mode": self.mode, "pass": "1", "pid": "11"}
                        if self.mode == "reader-hold":
                            return {**base, "pid": "12", "rc": "0"}
                        if self.mode == "writer-try":
                            writer_count[0] += 1
                            if writer_count[0] == 1:
                                return {**base, "pid": "13", "pass": "0", "rc": "-1", "errno": "5"}
                            return {**base, "pid": "14", "rc": "-1" if release_busy else "0",
                                    "errno": "35" if release_busy else "0"}
                        if self.mode == "cleanup":
                            return {**base, "pid": "15"}
                        return base
                    def terminate(self, command):
                        trace.append("terminate:" + command)
                    def close(self):
                        trace.append("close:" + self.mode)
                with mock.patch.object(runner, "Payload", FakePayload):
                    result = runner.run("unused", 9305, b"elf", self.nonce, [])
                self.assertFalse(result["pass"])
                self.assertIn("terminate:return", trace)
                last_writer = max(i for i, item in enumerate(trace) if item == "start:writer-try")
                self.assertLess(trace.index("terminate:return"), last_writer)
                if release_busy:
                    self.assertNotIn("start:cleanup", trace)
                    self.assertIsNone(result["cleanup"])
                else:
                    self.assertGreater(trace.index("start:cleanup"), last_writer)
                    self.assertEqual(result["cleanup"]["pass"], "1")

    def test_noise_cannot_be_an_acceptance_record(self):
        self.assertIsNone(runner.parse_record("payload loader ready", self.nonce, "init"))
        with self.assertRaises(ValueError):
            runner.parse_record(f"PWPA1 run={self.nonce} mode=init pid=7 pass=1",
                                self.nonce, "init")


if __name__ == "__main__":
    unittest.main()
