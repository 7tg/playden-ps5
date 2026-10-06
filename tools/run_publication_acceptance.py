#!/usr/bin/env python3
"""Run isolated payload diagnostics after the operator has claimed the PS5."""
import argparse
import hashlib
import json
from pathlib import Path
import secrets
import re
import socket
import time

REVISION = "79cf85c065e49ae57679ff5babb88bd290b9e9e9"
FOUNDATION = "9c0b994a048521af6fb84c73ded364504fe250e9"
PROVIDER = "f14d5e3ecb31951e3cf5bfdc41972181a9be1d96ed0dc7cb3aeebb8e4aab844a"


def parse_record(line, nonce, mode):
    if not line.startswith("PWPA1 "):
        return None
    fields = {}
    for token in line.split()[1:]:
        if "=" not in token:
            raise ValueError("Malformed diagnostic record: " + line)
        key, value = token.split("=", 1)
        if not key or not value or key in fields:
            raise ValueError("Missing or duplicate diagnostic field: " + line)
        fields[key] = value
    if fields.get("run") != nonce or fields.get("mode") != mode:
        raise ValueError("Foreign diagnostic record: " + line)
    if not fields.get("stage") or not re.fullmatch(r"[1-9][0-9]*", fields.get("pid", "")):
        raise ValueError("Diagnostic record lacks a valid stage/process: " + line)
    return fields


def require_writer(event, busy):
    expected = "-1" if busy else "0"
    if (event.get("stage") != "result" or event.get("mode") != "writer-try"
            or event.get("pass") != "1" or event.get("rc") != expected
            or event.get("errno") != ("35" if busy else "0")):
        raise ValueError("Writer did not demonstrate expected PS5 lock state: " + repr(event))


def require_reader(event, expected):
    if (event.get("stage") != "result" or event.get("mode") != "reader-try"
            or event.get("pass") != "1" or event.get("rc") != str(expected)
            or (expected == -1 and event.get("errstage") != "1")):
        raise ValueError("Reader did not demonstrate expected admission: " + repr(event))


def validate_manifest(manifest_path, reviewed_digest, root, foundation):
    data = manifest_path.read_bytes()
    if hashlib.sha256(data).hexdigest() != reviewed_digest:
        raise ValueError("Diagnostic manifest differs from the independently reviewed digest")
    manifest = json.loads(data)
    if (manifest.get("schema") != "playden-publication-acceptance/1"
            or manifest.get("prospero_revision") != REVISION
            or manifest.get("foundation_revision") != FOUNDATION
            or manifest.get("publication_provider_sha256") != PROVIDER
            or manifest.get("elf") != "publication-acceptance.elf"
            or manifest.get("acceptance_scope") != "standalone-payload; no granted-wrapper lifetime claim"):
        raise ValueError("Diagnostic provenance does not match independently pinned inputs")
    for relative in ("probes/publication-acceptance/main.c", "src/platform/ps5/file_shims.c"):
        expected = manifest.get("inputs", {}).get(relative)
        if hashlib.sha256((root / relative).read_bytes()).hexdigest() != expected:
            raise ValueError("Reviewed diagnostic input is stale: " + relative)
    inventory = manifest.get("sdk_inputs")
    if not isinstance(inventory, dict) or not inventory:
        raise ValueError("SDK inventory is missing")
    frozen_inventory = json.dumps(inventory, sort_keys=True, separators=(",", ":")).encode()
    if hashlib.sha256(frozen_inventory).hexdigest() != manifest.get("sdk_inputs_sha256"):
        raise ValueError("SDK inventory digest is inconsistent")
    sdk = foundation.resolve() / ".deps/native/ps5-payload-sdk"
    for relative, expected in inventory.items():
        path = Path(relative)
        if path.is_absolute() or ".." in path.parts:
            raise ValueError("SDK inventory path is invalid")
        if hashlib.sha256((sdk / path).read_bytes()).hexdigest() != expected:
            raise ValueError("Reviewed SDK input changed: " + relative)
    elf = (manifest_path.parent / manifest["elf"]).read_bytes()
    if hashlib.sha256(elf).hexdigest() != manifest.get("elf_sha256"):
        raise ValueError("Diagnostic ELF differs from its build manifest")
    return manifest, elf, data


class Payload:
    def __init__(self, host, port, elf, nonce, mode, transcript):
        self.nonce, self.mode, self.transcript = nonce, mode, transcript
        self.pending = b""
        self.events = []
        self.socket = socket.create_connection((host, port), timeout=10)
        try:
            self.socket.settimeout(10)
            self.socket.sendall(elf)
            self.socket.sendall(f"PWPA1 {nonce} {mode}\n".encode())
        except BaseException:
            self.close()
            raise

    def read_record(self):
        while b"\n" not in self.pending:
            chunk = self.socket.recv(8192)
            if not chunk:
                if self.pending:
                    raise ValueError("Truncated diagnostic output")
                raise EOFError("Payload terminated")
            self.pending += chunk
            if len(self.pending) > 65536:
                raise ValueError("Unbounded diagnostic output")
        line, self.pending = self.pending.split(b"\n", 1)
        line = line.decode("utf-8", errors="strict").rstrip("\r")
        self.transcript.append(line)
        event = parse_record(line, self.nonce, self.mode)
        if event:
            if any(prior["stage"] == event["stage"] for prior in self.events):
                raise ValueError("Duplicate diagnostic stage")
            if self.events and self.events[0]["pid"] != event["pid"]:
                raise ValueError("Diagnostic changed process identity")
            self.events.append(event)
        return event

    def await_stage(self, stage):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            event = self.read_record()
            if not event:
                continue
            if event["stage"] == stage:
                return event
            if event["stage"] == "result" and stage != "result":
                raise ValueError("Payload refused before requested stage: " + repr(event))
        raise TimeoutError("Diagnostic stage timed out")

    def terminate(self, command):
        self.socket.sendall((command + "\n").encode())
        event = self.await_stage("terminating")
        if event.get("command") != command:
            raise ValueError("Termination command was not acknowledged")
        # EOF is evidence of transport closure, not itself proof of lock release.
        # A new independent process must subsequently obtain the writer lock.
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            try:
                event = self.read_record()
                if event and event["stage"] == "result" and event.get("pass") == "0":
                    raise ValueError("Holder termination failed")
            except EOFError:
                return
        raise TimeoutError("Holder did not terminate")

    def close(self):
        self.socket.close()


def run(host, port, elf, nonce, transcript):
    processes = set()
    holders = []

    def start(mode):
        payload = Payload(host, port, elf, nonce, mode, transcript)
        return payload

    def once(mode):
        payload = start(mode)
        try:
            event = payload.await_stage("result")
            if int(event["pid"]) in processes:
                raise ValueError("Diagnostic reused a prior process identity")
            processes.add(int(event["pid"]))
            return event
        finally:
            payload.close()

    def hold(mode):
        payload = start(mode)
        holders.append(payload)
        event = payload.await_stage("ready")
        if int(event["pid"]) in processes or event.get("rc") != "0" or event.get("pass") != "1":
            raise ValueError("Holder was not a fresh admitted process")
        processes.add(int(event["pid"]))
        return payload

    initialized = False
    failure = None
    cleanup = None
    scenarios = []
    try:
        init = once("init")
        if init.get("pass") != "1":
            raise ValueError("Exclusive canonical fixture initialization failed")
        initialized = True
        for command in ("return", "_exit", "kill"):
            holder = hold("reader-hold")
            require_writer(once("writer-try"), busy=True)
            holder.terminate(command)
            holder.close()
            holders.remove(holder)
            require_writer(once("writer-try"), busy=False)
            scenarios.append("reader-release-" + command)
        writer = hold("writer-pending-hold")
        require_reader(once("reader-try"), expected=-2)
        writer.terminate("kill")
        writer.close()
        holders.remove(writer)
        require_writer(once("writer-try"), busy=False)
        require_reader(once("reader-try"), expected=-1)
        scenarios.append("pending-refusal-after-publisher-death")
    except (OSError, ValueError, EOFError, TimeoutError) as error:
        failure = str(error)
    finally:
        for payload in holders:
            try:
                payload.terminate("return")
            except (OSError, ValueError, EOFError, TimeoutError):
                # The bounded payload also exits on EOF. Acquisition below is
                # the independent proof that it stopped retaining its lease.
                pass
            finally:
                payload.close()
        if initialized:
            try:
                require_writer(once("writer-try"), busy=False)
                cleanup = once("cleanup")
                if cleanup.get("pass") != "1":
                    raise ValueError("Identity-checked cleanup did not complete")
            except (OSError, ValueError, EOFError, TimeoutError) as error:
                failure = (failure + "; " if failure else "") + "cleanup: " + str(error)
    return {
        "pass": failure is None and len(scenarios) == 4,
        "failure": failure, "scenarios": scenarios, "cleanup": cleanup,
        "process_ids": sorted(processes, key=int),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=Path("build/publication-acceptance/manifest.json"))
    parser.add_argument("--reviewed-manifest-sha256", required=True)
    parser.add_argument("--foundation", type=Path, required=True)
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, default=9021)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--console-claimed", action="store_true", required=True,
                        help="Operator has an active console claim; release it after evidence collection")
    args = parser.parse_args()
    manifest, elf, manifest_bytes = validate_manifest(args.manifest, args.reviewed_manifest_sha256,
                                      Path(__file__).resolve().parents[1], args.foundation)
    if args.output.exists():
        raise ValueError("Evidence destination already exists")
    args.output.mkdir(parents=True)
    (args.output / "manifest.json").write_bytes(manifest_bytes)
    nonce = secrets.token_hex(8)
    transcript = []
    result = run(args.host, args.port, elf, nonce, transcript)
    text = "\n".join(transcript) + "\n"
    (args.output / "transcript.log").write_text(text)
    result.update({
        "schema": "playden-publication-acceptance-result/1", "run": nonce,
        "elf_sha256": manifest["elf_sha256"],
        "prospero_revision": REVISION,
        "foundation_revision": FOUNDATION,
        "publication_provider_sha256": PROVIDER,
        "manifest_sha256": args.reviewed_manifest_sha256,
        "sdk_inputs_sha256": manifest["sdk_inputs_sha256"],
        "transcript_sha256": hashlib.sha256(text.encode()).hexdigest(),
        "scope": "standalone-payload; granted wrapper/Wine lifetime remains untested",
    })
    (args.output / "acceptance.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps(result, indent=2, sort_keys=True))
    raise SystemExit(0 if result["pass"] else 1)


if __name__ == "__main__":
    main()
