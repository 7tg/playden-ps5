# Playden direct ps5-homebrew-ui candidate

Work in progress on `feat/ui-homebrew`. This candidate uses direct Hui with no RmlFocus or RmlUi presentation linkage. Comparison scope is defined in `docs/superpowers/specs/2026-10-04-ui-framework-comparison.md`.

Launcher baseline: `7e1c8d201aec23739eb1d598881fd6afba49a0d5` (comparison brief; implementation baseline described there is `16ea24f8378e282c8bdae2637a1beef3c353d364`). macOS reference: `de952b3d49167e635945ada88cd8533e07d40d44`. Hui baseline: `8a716f294835e26a24ef5612f1535aabffef4080`.

All authentication, storage, job and session behavior is local simulation. Profiles are isolated from real launcher state. No console deployment or sustained performance profiling is authorized by this candidate run.
