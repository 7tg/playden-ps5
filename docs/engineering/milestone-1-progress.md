# Milestone 1 execution record

Scope: approved tasks 1A–1B only. Steam, fixture libraries, performance acceptance, installation and Wine integration are deferred.

| Task | Interfaces and consistency check | Progress |
| --- | --- | --- |
| 1A | Platform creates devices and RmlUi context; Application owns product state. Actual patched graphical renderer is required separately from behavioral tests. | Complete; both builds and real graphical consumer verified. |
| 1B | Shell consumes rf::InputEvent and RmlRouter. Storage owns Bytes/Result; preferences consume that API. Platform measures display/memory, diagnostics consumes measurements. | Complete; host behavior and PS5 navigation/persistence/exit verified. |
| 1A + 1B | Common Application lifecycle and asset-root/storage-root contract agreed before platform implementation. | Integrated and verified. |

Ruling: work in the user-requested checkout on `milestone-1-shell`; no extra worktree is needed for this empty planning repository.

Ruling: use agent-team role/model settings as explicitly requested. Explorer's gpt-5.6-terra is unavailable; lead performs reference investigation. Worker roles use gpt-5.6-sol/medium; independent reviewer will use gpt-5.6-sol/high.

Reference inspection: `playden` branch `feature/launchers` resolves to `de952b3d49167e635945ada88cd8533e07d40d44`. Read `App/LibraryViews.swift` and `App/HomeNavigation.swift` using `git show`; checkout untouched. Adopt Home/Library/Settings hierarchy, controller hints, separate selected tab and content focus, exact opener restoration, and visible save errors. No Swift, fixtures, or runtime commands imported.

Shell test first failed with `FAIL: shell opens Home` against stub behavior. Implemented real-document routing; standalone behavior checks passed. Full installed-package/font test and graphical acceptance remain distinct checks.

Console coordination: user confirmed PS5 available and authorized selecting a FAKE title ID. Local ps5mcp CLI connects through the app-owned controller service (no raw competing connection); initial snapshot shows home, human control false. `FAKE77031` absent from staged and installed directories, selected for launcher testing. No runtime title is reused.

Cross-review: the runtime rejected spawning a fourth child (`agent thread limit reached`) despite a completed worker. Existing gpt-5.6-sol/medium workers therefore reviewed components they did not implement; the requested separate gpt-5.6-sol/high reviewer could not be started. Storage worker reviewed application/shell/diagnostics/build, diagnostics worker reviewed storage/preferences. No worker's own code is counted as independent review.

Resolved review findings so far: locale-dependent JSON decimals (regression under de_DE.UTF-8), missing explicit block layout (real GL screenshot and failing geometric assertion), frame sampling at end instead of start, GL resource teardown ordering, stale asset copying, and source dirty-state validation. Binary hash enforcement and final graphical/console validation are in progress.

Host verification: three CTest suites pass. Shell suite includes actual RmlUi document layout, geometric D-pad movement, selected-tab state, L1/R1, modal trapping, exact opener restoration, exit cancellation, app shutdown/restart save recovery, and visible corrupt-file recovery. Fresh real OpenGL 3840x2160 capture inspected after layout correction; timed app exits with status 0.

Console first smoke: real EGL surface 3840x2160 and accepted 60 Hz reported; Home/Library/Settings rendered, sheet dismissed to its opener, reduce-motion saved through the worker thread, `/app0/playden-data` probe/readback succeeded, and Settings exit returned home with `exit status=0`. Captures are HDMI 1920x1080; they do not themselves prove render resolution.

Integration defect found from console evidence: PS5 `llvm-pie.elf` custom command had only order-only dependencies on object-library targets. The changed platform object was newer than the linked PIE, explaining absent memory records despite updated source. Fix requires explicit object-file dependencies and an incremental rebuild check before final console acceptance. First package is retained as superseded evidence.


## Final state

Task 1A: complete. Task 1B: complete. The final signed artifact `d7910b27…` passed the third console smoke run with preference restoration, fresh writes, sheet focus restoration, owned-heap memory records, native 3840×2160 surface/viewport and accepted 60 Hz configuration, and clean system-managed exit. The console is back home. All three host test suites pass and actual Mac GL captures were inspected.

The final review found no outstanding important code issues. Lifecycle and incremental-build findings were reproduced and fixed before acceptance. See [evidence](../../results/20261004T132145Z-shell/README.md) for exact artifacts and limitations. Physical-controller feel and sustained performance acceptance remain milestone 2 work; later milestones are unstarted.

Ruling: commit the tightly integrated 1A/1B implementation coherently, followed by the acceptance record, rather than introducing a nonbuilding intermediate commit whose platform sources require the shell/preferences interfaces.

Implementation committed as `262fff9`; acceptance artifacts and plan completion are committed separately.
