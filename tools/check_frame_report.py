#!/usr/bin/env python3
"""Validate Playden's native-4K60 console evidence report."""

import argparse
import json
import math
from pathlib import Path
import re
import statistics
import sys


INTERACTIVE_SECONDS = 600.0
MEMORY_SOAK_SECONDS = 1800.0
P99_LIMIT_MS = 18.0
MISSED_PRESENTATION_LIMIT = 0.001
FRAME_COVERAGE_RATIO = 0.95
FRAME_DURATION_TOLERANCE_SECONDS = 1.0
MEMORY_GROWTH_FLOOR_BYTES = 16 * 1024 * 1024
REQUIRED_INTERACTIVE_MARKERS = {
    "library-scroll",
    "artwork-delivery",
    "details-sheet-confirmation",
    "queue-update",
    "search-account-errors",
}
HEX_REVISION = re.compile(r"^[0-9a-fA-F]{7,64}$")
SHA256 = re.compile(r"^[0-9a-fA-F]{64}$")


def _is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _is_nonnegative_integer(value):
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def _mapping(value, path, errors):
    if not isinstance(value, dict):
        errors.append(f"{path} must be an object, not missing or null")
        return {}
    return value


def _required_text(mapping, key, path, errors):
    value = mapping.get(key)
    if not isinstance(value, str) or not value.strip():
        errors.append(f"{path}.{key} must be a non-empty string")
        return None
    return value


def _required_number(mapping, key, path, errors):
    value = mapping.get(key)
    if not _is_number(value):
        errors.append(f"{path}.{key} must be a finite measured number, not missing or null")
        return None
    return float(value)


def _marker_times(scenario, path, errors):
    markers = scenario.get("markers")
    if not isinstance(markers, list):
        errors.append(f"{path}.markers must be an array")
        return {}
    result = {}
    for index, marker in enumerate(markers):
        marker_path = f"{path}.markers[{index}]"
        if not isinstance(marker, dict):
            errors.append(f"{marker_path} must be an object")
            continue
        name = _required_text(marker, "name", marker_path, errors)
        timestamp = _required_number(marker, "time_seconds", marker_path, errors)
        if name is not None and timestamp is not None:
            if name in result:
                errors.append(f"{path}.markers contains duplicate marker {name!r}")
            result[name] = timestamp
    return result


def _nearest_rank_percentile(values, fraction):
    ordered = sorted(values)
    rank = max(1, math.ceil(fraction * len(ordered)))
    return ordered[rank - 1]


def _validate_revisions(report, errors):
    revision = _required_text(report, "build_revision", "report", errors)
    if revision is not None and not HEX_REVISION.fullmatch(revision):
        errors.append("report.build_revision must be a 7-64 digit hexadecimal revision")
    dependencies = _mapping(report.get("dependency_revisions"), "report.dependency_revisions", errors)
    for dependency in ("rmlfocus", "ps5_opengl"):
        value = _required_text(dependencies, dependency, "report.dependency_revisions", errors)
        if value is not None and not HEX_REVISION.fullmatch(value):
            errors.append(
                f"report.dependency_revisions.{dependency} must be a 7-64 digit hexadecimal revision"
            )


def _validate_display(report, errors):
    display = _mapping(report.get("display"), "report.display", errors)
    surface_width = _required_number(display, "surface_width", "report.display", errors)
    surface_height = _required_number(display, "surface_height", "report.display", errors)
    render_width = _required_number(display, "render_width", "report.display", errors)
    render_height = _required_number(display, "render_height", "report.display", errors)
    refresh_hz = _required_number(display, "refresh_hz", "report.display", errors)

    if surface_width is not None and surface_height is not None and (
        surface_width != 3840 or surface_height != 2160
    ):
        errors.append("report.display must show a native 3840x2160 surface")
    if render_width is not None and render_height is not None and (
        render_width != 3840 or render_height != 2160
    ):
        errors.append("report.display must show a native 3840x2160 render target")
    if refresh_hz is not None and refresh_hz < 59.0:
        errors.append("report.display.refresh_hz must be at least 59 Hz")

    expected_sources = {
        "surface_source": "egl_query_surface",
        "render_source": "gl_viewport",
        "refresh_source": "egl_get_display_mode_ps5",
    }
    for key, expected in expected_sources.items():
        source = _required_text(display, key, "report.display", errors)
        if source is not None and source != expected:
            errors.append(f"report.display.{key} must be {expected!r}; requested or HDMI values are not measurements")
    return refresh_hz


def _validate_capture(report, errors):
    capture = _mapping(
        report.get("native_framebuffer_capture"),
        "report.native_framebuffer_capture",
        errors,
    )
    width = _required_number(capture, "width", "report.native_framebuffer_capture", errors)
    height = _required_number(capture, "height", "report.native_framebuffer_capture", errors)
    if width is not None and height is not None and (width != 3840 or height != 2160):
        errors.append("native framebuffer capture must be 3840x2160")
    source = _required_text(capture, "source", "report.native_framebuffer_capture", errors)
    if source is not None and source != "framebuffer_readback":
        errors.append("native framebuffer capture source must be 'framebuffer_readback'")
    _required_text(capture, "artifact", "report.native_framebuffer_capture", errors)
    digest = _required_text(capture, "sha256", "report.native_framebuffer_capture", errors)
    if digest is not None and not SHA256.fullmatch(digest):
        errors.append("report.native_framebuffer_capture.sha256 must contain 64 hexadecimal digits")


def _validate_interactive(scenario, refresh_hz, errors):
    path = "report.scenarios.interactive"
    duration = _required_number(scenario, "duration_seconds", path, errors)
    if duration is not None and duration < INTERACTIVE_SECONDS:
        errors.append(f"{path}.duration_seconds must be at least 600 seconds")
    marker_times = _marker_times(scenario, path, errors)
    for name in sorted(REQUIRED_INTERACTIVE_MARKERS - marker_times.keys()):
        errors.append(f"{path}.markers is missing required marker {name!r}")
    if duration is not None:
        for name, timestamp in marker_times.items():
            if timestamp < 0 or timestamp > duration:
                errors.append(f"{path} marker {name!r} must be within the scenario duration")

    interval_source = _required_text(scenario, "frame_interval_source", path, errors)
    if interval_source is not None and interval_source != "monotonic_frame_start":
        errors.append(f"{path}.frame_interval_source must be 'monotonic_frame_start'")

    intervals = scenario.get("frame_intervals_ms")
    valid_intervals = []
    if not isinstance(intervals, list) or not intervals:
        errors.append(f"{path}.frame intervals must be a non-empty array; missing samples are not zero")
    else:
        for index, value in enumerate(intervals):
            if not _is_number(value) or value <= 0:
                errors.append(f"{path}.frame_intervals_ms[{index}] must be a positive finite measurement")
                continue
            valid_intervals.append(float(value))
        if valid_intervals:
            p99 = _nearest_rank_percentile(valid_intervals, 0.99)
            if p99 > P99_LIMIT_MS:
                errors.append(f"{path} frame-interval p99 {p99:.3f} ms exceeds 18 ms")
        if duration is not None and refresh_hz is not None:
            required = math.floor(duration * refresh_hz * FRAME_COVERAGE_RATIO)
            if len(valid_intervals) < required:
                errors.append(
                    f"{path} has {len(valid_intervals)} frame intervals; at least {required} are required for run coverage"
                )
        if duration is not None and valid_intervals:
            measured_seconds = sum(valid_intervals) / 1000.0
            if abs(measured_seconds - duration) > FRAME_DURATION_TOLERANCE_SECONDS:
                errors.append(
                    f"{path}.frame_intervals_ms span {measured_seconds:.3f} seconds and must "
                    f"span the claimed duration within {FRAME_DURATION_TOLERANCE_SECONDS:.0f} second"
                )

    presentation = _mapping(scenario.get("presentation"), f"{path}.presentation", errors)
    source = _required_text(presentation, "source", f"{path}.presentation", errors)
    if source is not None and source != "videoout_flip_status":
        errors.append(
            f"{path}.presentation.source must identify the hardware presentation source 'videoout_flip_status'"
        )
    counters = {}
    for key in ("presented_count", "missed_count", "max_consecutive_missed"):
        value = presentation.get(key)
        if not _is_nonnegative_integer(value):
            errors.append(f"{path}.presentation.{key} must be a measured non-negative integer, not missing or null")
        else:
            counters[key] = value
    if "presented_count" in counters and "missed_count" in counters:
        total = counters["presented_count"] + counters["missed_count"]
        if total == 0:
            errors.append(f"{path}.presentation has no measured presentation outcomes")
        else:
            missed_rate = counters["missed_count"] / total
            if missed_rate > MISSED_PRESENTATION_LIMIT:
                errors.append(f"{path} missed presentations {missed_rate:.4%} exceed 0.1%")
            if duration is not None and refresh_hz is not None:
                required = math.floor(duration * refresh_hz * FRAME_COVERAGE_RATIO)
                if total < required:
                    errors.append(
                        f"{path} has {total} presentation outcomes; at least {required} are required for run coverage"
                    )
                maximum = math.ceil((duration + FRAME_DURATION_TOLERANCE_SECONDS) * refresh_hz)
                if total > maximum:
                    errors.append(
                        f"{path} has too many presentation outcomes for the elapsed duration and refresh rate: "
                        f"{total} exceeds {maximum}"
                    )
    if counters.get("max_consecutive_missed", 0) >= 3:
        errors.append(f"{path} contains a burst of three consecutive missed presentations")
    if (
        "max_consecutive_missed" in counters
        and "missed_count" in counters
        and counters["max_consecutive_missed"] > counters["missed_count"]
    ):
        errors.append(f"{path}.presentation.max_consecutive_missed cannot exceed missed_count")
    if counters.get("missed_count", 0) > 0 and counters.get("max_consecutive_missed") == 0:
        errors.append(
            f"{path}.presentation.max_consecutive_missed must be at least one when missed_count is nonzero"
        )


def _linear_slope(samples):
    mean_time = statistics.fmean(time for time, _ in samples)
    mean_bytes = statistics.fmean(value for _, value in samples)
    denominator = sum((time - mean_time) ** 2 for time, _ in samples)
    if denominator == 0:
        return 0.0
    return sum((time - mean_time) * (value - mean_bytes) for time, value in samples) / denominator


def _validate_memory_soak(scenario, errors):
    path = "report.scenarios.memory_soak"
    duration = _required_number(scenario, "duration_seconds", path, errors)
    if duration is not None and duration < MEMORY_SOAK_SECONDS:
        errors.append(f"{path}.duration_seconds must be at least 1800 seconds")
    markers = _marker_times(scenario, path, errors)
    warm_start = markers.get("warm-cache-start")
    if warm_start is None:
        errors.append(f"{path}.markers is missing required marker 'warm-cache-start'")
    elif duration is not None and (warm_start < 0 or warm_start > duration):
        errors.append(f"{path} marker 'warm-cache-start' must be within the scenario duration")
    elif duration is not None and duration - warm_start < MEMORY_SOAK_SECONDS:
        errors.append(f"{path} must measure at least 1800 seconds after warm-cache-start")
    source = _required_text(scenario, "memory_source", path, errors)
    if source is not None and source != "process_resident_bytes":
        errors.append(f"{path}.memory_source must be 'process_resident_bytes'")

    raw_samples = scenario.get("memory_samples")
    samples = []
    if not isinstance(raw_samples, list) or not raw_samples:
        errors.append(f"{path}.memory samples must be a non-empty array; missing samples are not zero")
        return
    for index, sample in enumerate(raw_samples):
        sample_path = f"{path}.memory_samples[{index}]"
        if not isinstance(sample, dict):
            errors.append(f"{sample_path} must be an object")
            continue
        timestamp = _required_number(sample, "time_seconds", sample_path, errors)
        used_bytes = sample.get("used_bytes")
        if not _is_nonnegative_integer(used_bytes) or used_bytes == 0:
            errors.append(f"{sample_path}.used_bytes must be a positive measured integer")
            continue
        if timestamp is not None:
            samples.append((timestamp, used_bytes))
    if any(current[0] <= previous[0] for previous, current in zip(samples, samples[1:])):
        errors.append(f"{path}.memory_samples timestamps must be strictly increasing")
    if warm_start is None or duration is None:
        return
    post_warm = [sample for sample in samples if warm_start <= sample[0] <= duration]
    if len(post_warm) < 20:
        errors.append(f"{path} requires at least 20 warm-cache memory samples")
        return
    if post_warm[0][0] > warm_start + 60 or post_warm[-1][0] < duration - 60:
        errors.append(f"{path} memory samples must cover warm-cache start through the end of the soak")
    window = max(3, math.ceil(len(post_warm) * 0.2))
    initial_median = statistics.median(value for _, value in post_warm[:window])
    final_median = statistics.median(value for _, value in post_warm[-window:])
    material_growth = max(MEMORY_GROWTH_FLOOR_BYTES, initial_median * 0.05)
    if final_median - initial_median > material_growth and _linear_slope(post_warm) > 0:
        errors.append(
            f"{path} shows persistent warm-cache memory growth: median rose "
            f"{final_median - initial_median:.0f} bytes"
        )


def validate_report(report):
    errors = []
    if not isinstance(report, dict):
        return ["report must be a JSON object"]
    if report.get("schema_version") != 1:
        errors.append("report.schema_version must equal 1")
    _required_text(report, "run_id", "report", errors)
    if report.get("platform") != "ps5":
        errors.append("report.platform must equal 'ps5'")
    _required_text(report, "console_environment", "report", errors)
    _validate_revisions(report, errors)
    refresh_hz = _validate_display(report, errors)
    _validate_capture(report, errors)
    scenarios = _mapping(report.get("scenarios"), "report.scenarios", errors)
    interactive = _mapping(scenarios.get("interactive"), "report.scenarios.interactive", errors)
    memory_soak = _mapping(scenarios.get("memory_soak"), "report.scenarios.memory_soak", errors)
    _validate_interactive(interactive, refresh_hz, errors)
    _validate_memory_soak(memory_soak, errors)
    return errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path, help="JSON report to validate")
    arguments = parser.parse_args(argv)
    try:
        with arguments.report.open("r", encoding="utf-8") as report_file:
            report = json.load(report_file)
    except (OSError, json.JSONDecodeError) as error:
        print(f"ERROR: could not read report: {error}", file=sys.stderr)
        return 2
    errors = validate_report(report)
    if errors:
        for error in errors:
            print(f"REJECT: {error}", file=sys.stderr)
        return 1
    print("PASS: report contains complete native 4K60 evidence")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
