# Coverage and acceptance evidence

The status below is for the deterministic macOS host candidate. `implemented-and-verified` means the stated simulation behavior was exercised by model/controller tests or matching captures. It does not establish physical DualSense operation or PS5 execution. Native PS5 compilation/link/sign/package is verified separately; console execution remains `implemented-unverified` until coordinated access.

Evidence abbreviations: **M** = `homebrew_simulation` acceptance/transition tests; **C** = `homebrew_controller_tour` visible-control traversal; **B** = `homebrew_browse` real-fixture filtering, sorting, shelf/rail navigation and UTF-8 tests; **S** = `homebrew_storage` accounting tests; **I** = `homebrew_host_input` actual SDL/kit input adapter regressions; **V** = matching capture assertions plus visual inspection. The [CTest output](test-output/ctest.log) records the integrated run; [captures](test-output/captures.log) record all common definitions.

## Surfaces

| ID | Status | Evidence / boundary |
| --- | --- | --- |
| U01 | implemented-and-verified | Connect/Skip, isolated restore, corrupt/save-unavailable recovery and controlled startup loading; M/C |
| U02 | implemented-and-verified | Explicit simulated QR, expiry/refresh/failure/denial/current approval and late-cancel isolation; M/C/V |
| U03 | implemented-and-verified | Account/sync/offline recovery, signed-out identity/cache isolation, retry/sign-in; M; simulation only |
| U04 | implemented-and-verified | Nonempty state-derived shelves, per-shelf child focus and exact details return; B/C/V |
| U05 | implemented-and-verified | Six-column grid, rail/scopes, fixed poster pitch, empty/no-results/loading/offline, refresh identity, art fallback and bounded rows; B/M/V. Texture cache is nine preloaded fixture textures |
| U06 | implemented-and-verified | Controller keyboard, UTF-8 caret/backspace, shift/symbols/space/Clear, Done/Cancel restoration; B/C/I/V |
| U07 | implemented-and-verified | All five sorts; installation/genre/controller/compatibility filters; honest unknown metadata; reset/count/selection reconciliation; B/M/V |
| U08 | implemented-and-verified | State-dependent actions, missing-data fallback, description/metadata and paged details content; V/M |
| U09 | implemented-and-verified | Same game context through Options/More, disabled capability reasons, favorite/hide/collections/rating/verify/uninstall/logs; M/C/V |
| U10 | implemented-and-verified | Create/rename/validation, membership/pin/delete, Cancel default and preserved games; M/C |
| U11 | implemented-and-verified | Local compatibility and optional note persist; C/M; never a runtime capability claim |
| U12 | implemented-and-verified | Optional sizing, installed-footprint checks, destination recheck, unknown estimate retry and idempotent submission; S/M/V |
| U13 | implemented-and-verified | Capacity/used/reserved/free and writable/readonly/disconnected destinations; S/M/V; no writable game path |
| U14 | implemented-and-verified | Active card/queue/recent records, exact bytes, manual-clock stages/branches and global indicator; M/S/V |
| U15 | implemented-and-verified | Pause/resume/reorder/cancel/retry, safe confirmation, stable unique IDs, open/log/dismiss and selection recovery; M/C |
| U16 | implemented-and-verified | Serialized verify, uninstall busy/result/failure/retry, reinstall, saves/unrelated baseline preservation; M/S |
| U17 | implemented-and-verified | Grouped rows, inherited/custom values, per-game overrides, reset one/all, nested editor/picker and save failure; M/C/V |
| U18 | implemented-and-verified | Default/custom selection, summaries, per-game persisted selection and parent restoration; M/V |
| U19 | implemented-and-verified | Simulated unavailable/checking/ready/failed/retry; independent from browsing/downloads and install completion; M/S |
| U20 | implemented-and-verified | Simulated preparation/launch/running/error/retry/crash/back; M/V; no process launching |
| U21 | implemented-and-verified | In-app Return/Quit, stopping, failed-stop recovery and safe force-stop; M/C/V; no OS-wide overlay/PS-button hook |
| U22 | implemented-and-verified | Account/storage/appearance/controller/runtime/About/logs/reset/quit with shared rows and safe dialogs; M/C/V |
| U23 | implemented-and-verified | Logical disconnect/reconnect retains modal focus; actual SDL/kit held-button/stick/trigger mapping drives diagnostics; I/M. Physical pad feel unverified |
| U24 | implemented-and-verified | Bounded fixture log records, loading/empty/failure/retry, context, scrolling and back; M/V |
| U25 | implemented-and-verified | Explicit sign-out/reset/quit impact, Cancel default, declared install/save/history preservation; M/C/V |
| U26 | implemented-and-verified | Toasts plus persistent warning actions, completion destination, connection/storage/save recovery without focus theft; M |
| U27 | excluded-by-brief | Password/Steam Guard/device-code/web authentication |
| U28 | excluded-by-brief | Cloud saves/conflicts/recovery copies |
| U29 | excluded-by-brief | Other stores/publisher accounts/web content |
| U30 | excluded-by-brief | Community profiles/feedback services |
| U31 | excluded-by-brief | Local Mac games/folders/Finder |
| U32 | excluded-by-brief | Mac setup/device controls/audio/layout preference |

## Shared acceptance tour

| Check | Status | Evidence |
| --- | --- | --- |
| T01 | implemented-and-verified | M tests cancelled-generation isolation; C walks Connect → expire → refresh → approve → sync → Home through visible controls |
| T02 | implemented-and-verified | M exact game/viewport/context opener; C/B Home shelf child restoration |
| T03 | implemented-and-verified | M edit cancel/query/reset/removal; B real counts/filter/sort/edge rules; I printable-text isolation |
| T04 | implemented-and-verified | M persisted organization/rating/note; C membership/pin/delete safety; collection deletion preserves 24 games |
| T05 | implemented-and-verified | M destination disconnect before submit and duplicate prevention; S installed-size/free-space/readonly/unknown checks |
| T06 | implemented-and-verified | M queue/stage/retry/cancel/one-active/unique-ID regressions; S reserved-to-used accounting |
| T07 | implemented-and-verified | M interrupted restart/storage/verification retry; S download completion leaves runtime unavailable |
| T08 | implemented-and-verified | M/C nested picker/editor openers; per-game custom-profile isolation/restart |
| T09 | implemented-and-verified | M uninstall Cancel/busy/completion/reinstall and reset; S preserves saves and unrelated bytes |
| T10 | implemented-and-verified | M simulated launch/error/running/stop/crash; C walks preparation/manual clock/running/quit/stop failure/force-stop through visible controls |
| T11 | implemented-and-verified | M reduced-motion persistence/save failure/modal reconnect; I actual adapter mapping; C bounded log/session recovery and 1,000-game controller paging |
| T12 | implemented-and-verified | M traverses 1,000 logical games with <=24 materialized rows and stable ID while manual art/job events advance; V supplemental stress render diagnostics verify global focused game and <=24 cell callbacks; cache remains nine shared textures |

## Limitations and remaining platform work

The frozen fonts lack Greek accented alpha/omicron U+03AC/U+03CC. These remain visible missing-glyph limitations with the original title bytes preserved. A coordinated shared-font revision is needed to fix them. The candidate deliberately reports Arabic glyphs without shaping or bidirectional layout, whole-string fallback, and an opaque warm panel/dim scrim instead of the reference blur. These are visible fidelity gaps, not hidden English substitutions. See [framework-gap.md](framework-gap.md).

The deterministic delayed-art event validates placeholder-to-art arrival with unchanged geometry. Actual background file decoding, upload scheduling, eviction and generation cancellation are **blocked** in this candidate's artwork pipeline: the current implementation preloads the nine shared images once. Recovery work is a background decoder plus a bounded render-thread upload queue and generation-aware cache. This is a component-level limitation even though the U05/T12 fixture state transition and bound are verified.

Native title execution, writable comparison-profile behavior on the console, physical pad sampling and displayed output remain **implemented-unverified**. Recovery work is a coordinated console run of the packaged candidate and the same tour. No shared-console deployment was performed. No native 4K60, frame-time, latency, RSS or sustained performance conclusion is supported; deferred profiling remains unscored.
