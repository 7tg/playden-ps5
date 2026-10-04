# Native phase baseline

A short static 3840×2160 library run with developer fixtures, four-sample MSAA,
and framebuffer capture disabled. R1 entered Library three seconds after launch.
Options exited cleanly. Local user ID and pad handle are redacted from the log.

After artwork settled (8 cached images, zero queued/in flight), representative
five-second windows report input/tick/update p50 about 1.8 ms,
clear/begin/render submission p50 about 28.6 ms, end submission about 0.2 ms,
and swap combined wait about 6.1 ms. Stage durations are CPU wall time; rendering
submission can include driver waits and does not measure GPU execution alone.
This contradicts a swap-dominant explanation; further rendering-phase profiling
is required. Capture overhead cannot explain this run.

The build predates the final phase-key rename: `update_cpu` includes input, tick,
and context update; `render_submission_cpu` includes clear, begin, and render.
The committed helper labels those boundaries explicitly.

This short diagnostic is not the ten-minute scenario or thirty-minute soak.
Intervals exceed the 4K60 target; RSS and hardware presentation counts remain
unavailable, and no performance acceptance is claimed.
