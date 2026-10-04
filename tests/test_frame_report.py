import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "check_frame_report.py"


def load_checker():
    spec = importlib.util.spec_from_file_location("check_frame_report", TOOL)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def marker(name, time_seconds):
    return {"name": name, "time_seconds": time_seconds}


def valid_report():
    frame_count = 36_000
    memory_samples = [
        {"time_seconds": float(second), "used_bytes": 500_000_000 + (second % 4) * 1024}
        for second in range(300, 2101, 60)
    ]
    return {
        "schema_version": 1,
        "run_id": "2026-10-04-ps5-4k60",
        "platform": "ps5",
        "console_environment": "PS5 firmware 13.60; native title",
        "build_revision": "0123456789abcdef",
        "dependency_revisions": {
            "rmlfocus": "be7b7e48875ab5b87d2184ae57caed6a58c47cf8",
            "ps5_opengl": "dce74910af7f4274c48d05e0ae001b3038462588",
        },
        "display": {
            "surface_width": 3840,
            "surface_height": 2160,
            "surface_source": "egl_query_surface",
            "render_width": 3840,
            "render_height": 2160,
            "render_source": "gl_viewport",
            "refresh_hz": 60.0,
            "refresh_source": "egl_get_display_mode_ps5",
        },
        "native_framebuffer_capture": {
            "width": 3840,
            "height": 2160,
            "source": "framebuffer_readback",
            "artifact": "captures/library-native.ppm",
            "sha256": "a" * 64,
        },
        "scenarios": {
            "interactive": {
                "duration_seconds": 600.0,
                "markers": [
                    marker("library-scroll", 5),
                    marker("artwork-delivery", 120),
                    marker("details-sheet-confirmation", 240),
                    marker("queue-update", 360),
                    marker("search-account-errors", 480),
                ],
                "frame_interval_source": "monotonic_frame_start",
                "frame_intervals_ms": [16.67] * frame_count,
                "presentation": {
                    "source": "videoout_flip_status",
                    "presented_count": frame_count,
                    "missed_count": 0,
                    "max_consecutive_missed": 0,
                },
            },
            "memory_soak": {
                "duration_seconds": 2100.0,
                "markers": [marker("warm-cache-start", 300)],
                "memory_source": "process_resident_bytes",
                "memory_samples": memory_samples,
            },
        },
    }


class FrameReportCheckerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.checker = load_checker()

    def assert_rejected(self, report, phrase):
        errors = self.checker.validate_report(report)
        self.assertTrue(errors, "report unexpectedly passed")
        self.assertTrue(
            any(phrase in error for error in errors),
            f"expected {phrase!r} in errors: {errors}",
        )

    def test_accepts_complete_native_4k60_evidence(self):
        self.assertEqual([], self.checker.validate_report(valid_report()))

    def test_rejects_1080p_surface_or_render_target(self):
        report = valid_report()
        report["display"]["render_width"] = 1920
        report["display"]["render_height"] = 1080
        self.assert_rejected(report, "native 3840x2160 render target")

    def test_rejects_30_hz_refresh(self):
        report = valid_report()
        report["display"]["refresh_hz"] = 30
        self.assert_rejected(report, "at least 59 Hz")

    def test_missing_or_null_measurements_never_mean_zero(self):
        mutations = [
            (("display", "surface_width"), None, "surface_width"),
            (("scenarios", "interactive", "frame_intervals_ms"), [], "frame intervals"),
            (("scenarios", "interactive", "frame_interval_source"), None, "frame_interval_source"),
            (("scenarios", "interactive", "presentation", "missed_count"), None, "missed_count"),
            (("scenarios", "memory_soak", "memory_samples"), [], "memory samples"),
            (("dependency_revisions", "ps5_opengl"), None, "ps5_opengl"),
        ]
        for path, value, phrase in mutations:
            with self.subTest(path=path):
                report = valid_report()
                target = report
                for key in path[:-1]:
                    target = target[key]
                target[path[-1]] = value
                self.assert_rejected(report, phrase)

    def test_rejects_average_60_trace_that_hides_repeated_stalls(self):
        report = valid_report()
        intervals = [16.19] * 35_280 + [40.0] * 720
        self.assertAlmostEqual(16.6662, sum(intervals) / len(intervals), places=3)
        report["scenarios"]["interactive"]["frame_intervals_ms"] = intervals
        self.assert_rejected(report, "p99")

    def test_rejects_p99_over_18_ms(self):
        report = valid_report()
        report["scenarios"]["interactive"]["frame_intervals_ms"] = [18.01] * 36_000
        self.assert_rejected(report, "p99")

    def test_rejects_interval_count_that_does_not_span_claimed_duration(self):
        report = valid_report()
        report["scenarios"]["interactive"]["frame_intervals_ms"] = [1.0] * 36_000
        self.assert_rejected(report, "span the claimed duration")

    def test_rejects_more_than_point_one_percent_hardware_misses(self):
        report = valid_report()
        presentation = report["scenarios"]["interactive"]["presentation"]
        presentation["presented_count"] = 35_963
        presentation["missed_count"] = 37
        self.assert_rejected(report, "0.1%")

    def test_rejects_three_consecutive_hardware_misses(self):
        report = valid_report()
        presentation = report["scenarios"]["interactive"]["presentation"]
        presentation["presented_count"] = 35_997
        presentation["missed_count"] = 3
        presentation["max_consecutive_missed"] = 3
        self.assert_rejected(report, "three consecutive")

    def test_rejects_inconsistent_presentation_counts(self):
        report = valid_report()
        presentation = report["scenarios"]["interactive"]["presentation"]
        presentation["missed_count"] = 0
        presentation["max_consecutive_missed"] = 1
        self.assert_rejected(report, "cannot exceed missed_count")

    def test_rejects_zero_consecutive_count_when_misses_were_measured(self):
        report = valid_report()
        presentation = report["scenarios"]["interactive"]["presentation"]
        presentation["presented_count"] = 35_999
        presentation["missed_count"] = 1
        presentation["max_consecutive_missed"] = 0
        self.assert_rejected(report, "must be at least one")

    def test_rejects_more_presentation_outcomes_than_elapsed_refresh_slots(self):
        report = valid_report()
        presentation = report["scenarios"]["interactive"]["presentation"]
        presentation["presented_count"] = 72_000
        self.assert_rejected(report, "too many presentation outcomes")

    def test_rejects_estimated_presentation_source(self):
        report = valid_report()
        report["scenarios"]["interactive"]["presentation"] = {
            "source": "estimated_from_frame_intervals",
            "presented_count": 36_000,
            "missed_count": 0,
            "max_consecutive_missed": 0,
        }
        self.assert_rejected(report, "hardware presentation source")

    def test_rejects_persistent_warm_cache_memory_growth(self):
        report = valid_report()
        report["scenarios"]["memory_soak"]["memory_samples"] = [
            {"time_seconds": float(second), "used_bytes": 500_000_000 + second * 100_000}
            for second in range(300, 2101, 60)
        ]
        self.assert_rejected(report, "persistent warm-cache memory growth")

    def test_rejects_soak_shorter_than_thirty_minutes_after_warmup(self):
        report = valid_report()
        soak = report["scenarios"]["memory_soak"]
        soak["duration_seconds"] = 1800.0
        soak["memory_samples"] = [sample for sample in soak["memory_samples"] if sample["time_seconds"] <= 1800]
        self.assert_rejected(report, "after warm-cache-start")

    def test_rejects_short_runs_and_missing_scenario_markers(self):
        report = valid_report()
        report["scenarios"]["interactive"]["duration_seconds"] = 599
        report["scenarios"]["memory_soak"]["duration_seconds"] = 1799
        report["scenarios"]["interactive"]["markers"].pop()
        errors = self.checker.validate_report(report)
        self.assertTrue(any("600 seconds" in error for error in errors))
        self.assertTrue(any("1800 seconds" in error for error in errors))
        self.assertTrue(any("search-account-errors" in error for error in errors))

    def test_rejects_markers_outside_the_scenario(self):
        report = valid_report()
        report["scenarios"]["interactive"]["markers"][0]["time_seconds"] = -1
        report["scenarios"]["memory_soak"]["markers"][0]["time_seconds"] = 2101
        errors = self.checker.validate_report(report)
        self.assertGreaterEqual(sum("within the scenario duration" in error for error in errors), 2)

    def test_cli_returns_nonzero_and_prints_all_rejections(self):
        report = valid_report()
        report["display"]["refresh_hz"] = 30
        report["scenarios"]["interactive"]["presentation"]["missed_count"] = None
        with tempfile.TemporaryDirectory() as directory:
            report_path = Path(directory) / "report.json"
            report_path.write_text(json.dumps(report), encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(TOOL), str(report_path)],
                text=True,
                capture_output=True,
                check=False,
            )
        self.assertEqual(1, result.returncode)
        self.assertIn("at least 59 Hz", result.stderr)
        self.assertIn("missed_count", result.stderr)


if __name__ == "__main__":
    unittest.main()
