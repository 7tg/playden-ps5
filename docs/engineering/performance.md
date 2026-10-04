# Native 4K60 performance evidence

Milestone 2B is an evidence gate. A build, a requested display mode, an average frame rate, or a 1080p HDMI capture does not establish native 4K60. Run the checker with:

```sh
python3 tools/check_frame_report.py results/<run-id>-4k60/report.json
```

The command exits 0 only for a complete passing report, 1 for measured or missing evidence that fails the gate, and 2 when the report cannot be read. Missing and `null` values are rejected rather than interpreted as zero.

## Acceptance runs

The report contains two named scenarios:

- `interactive`: at least 600 seconds at native 3840×2160 and at least 59 Hz. It must mark `library-scroll`, `artwork-delivery`, `details-sheet-confirmation`, `queue-update`, and `search-account-errors`. Raw positive start-to-start intervals must cover at least 95% of the expected refresh slots, and their sum must match the claimed duration within one second. Their nearest-rank p99 must be at most 18 ms.
- `memory_soak`: at least 1800 seconds after the `warm-cache-start` marker, with at least 20 timestamped resident-memory samples from that marker through the final minute. Warmup time is additional and does not count toward the 30-minute soak. The checker flags persistent growth when the final 20% sample median rises by more than both the noise allowance (16 MiB or 5% of the initial median, whichever is larger) and the fitted post-warmup trend is positive.

The interactive run also requires hardware presentation outcomes covering at least 95% of the elapsed refresh slots and no more than one extra second of slots. This rejects both incomplete and physically implausible counters while allowing for run-boundary sampling. Misses may be at most 0.1% of all presentation outcomes, any nonzero miss count requires a nonzero consecutive-miss count, and the maximum consecutive miss count must be below three. `estimated_from_frame_intervals` is invalid: a long frame is not proof that VideoOut dropped a presentation, and a short interval is not proof that it displayed one.

Loading and startup are recorded separately from the interactive interval. Physical DualSense review remains separate qualitative evidence and is not synthesized by this checker.

## Report schema

The checked JSON object has `schema_version: 1` and these required fields:

```json
{
  "schema_version": 1,
  "run_id": "2026-10-04-ps5-4k60",
  "platform": "ps5",
  "console_environment": "PS5 firmware 13.60; native title; display path description",
  "build_revision": "full-or-abbreviated-git-hex",
  "dependency_revisions": {
    "rmlfocus": "git-hex",
    "ps5_opengl": "git-hex"
  },
  "display": {
    "surface_width": 3840,
    "surface_height": 2160,
    "surface_source": "egl_query_surface",
    "render_width": 3840,
    "render_height": 2160,
    "render_source": "gl_viewport",
    "refresh_hz": 60,
    "refresh_source": "egl_get_display_mode_ps5"
  },
  "native_framebuffer_capture": {
    "width": 3840,
    "height": 2160,
    "source": "framebuffer_readback",
    "artifact": "captures/library-native.ppm",
    "sha256": "64 hexadecimal digits"
  },
  "scenarios": {
    "interactive": {
      "duration_seconds": 600,
      "markers": [{"name": "library-scroll", "time_seconds": 5}],
      "frame_interval_source": "monotonic_frame_start",
      "frame_intervals_ms": [16.67],
      "presentation": {
        "source": "videoout_flip_status",
        "presented_count": 1,
        "missed_count": 0,
        "max_consecutive_missed": 0
      }
    },
    "memory_soak": {
      "duration_seconds": 2100,
      "markers": [{"name": "warm-cache-start", "time_seconds": 300}],
      "memory_source": "process_resident_bytes",
      "memory_samples": [{"time_seconds": 300, "used_bytes": 500000000}, {"time_seconds": 2100, "used_bytes": 500001024}]
    }
  }
}
```

The short arrays above show shape only; they do not pass coverage checks. Marker times and memory sample times are seconds from their scenario start. Keep the report and sanitized logs under `results/<run-id>-4k60/`; keep large captures outside Git when needed and retain their path and SHA-256 in the report.

## PS5 measurement sources

Source inspection used RmlFocus `be7b7e48875ab5b87d2184ae57caed6a58c47cf8` and its ps5-opengl source checkout `dce74910af7f4274c48d05e0ae001b3038462588`.

After the first successful swap, query the window surface with `eglQuerySurface(..., EGL_WIDTH/EGL_HEIGHT, ...)`. A runtime-mode ps5-opengl SDK also provides `eglGetDisplayModePS5`; its documentation says the accepted refresh is final after the first presentation. Record those returned values. Requested `3840×2160@60`, the HDMI mode, and an overlay are not substitutes.

The current public ps5-opengl SDK does not expose presentation statistics. Its EGL swap path calls the internal `ps5_agc_gate2_present`. That runtime owns a private VideoOut handle, calls `sceVideoOutGetFlipStatus` into an opaque 16-word buffer, and only uses word 3 to match the submitted flip argument. It also counts successful submissions. The installed payload SDK exports `sceVideoOutGetFlipStatus` and `sceVideoOutGetVblankStatus`, but supplies no public status structures, and the application cannot access ps5-opengl's private VideoOut handle. Consequently none of these sources currently supports a trustworthy application-level missed-presentation count. `eglSwapBuffers` success, successful submission counts, completed markers, vblank waits, and frame-start timing must remain `unknown` for misses.

An achievable hardware path requires a documented VideoOut status ABI and access to the presenter's counters, preferably through a versioned public ps5-opengl query. It must report cumulative presentation outcomes and consecutive misses from the same VideoOut handle. Until that exists and is validated on the console, the checker's required `videoout_flip_status` evidence is unavailable and milestone 2B must fail rather than report zero.

For native visual evidence, capture one frame outside the timed interval. Bind the default read framebuffer, call `glReadPixels` for the queried 3840×2160 extent, write a PPM or equivalent lossless file, and record its dimensions and SHA-256. ps5-opengl's showcase example uses this method. This proves the native framebuffer content and complements the queried surface size. It does not prove the TV received a 4K signal; retain the 1080p HDMI capture only as separate interaction/output-path evidence.

Resident memory must come from a platform process-memory facility and be labeled `process_resident_bytes`. Allocator counters or cache sizes may be supplementary fields but cannot replace resident-memory samples for this gate.
