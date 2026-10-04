# Playden UI map and framework comparison brief

Date: 2026-10-04. Audience: the two implementation agents and the person comparing their builds.

Build the same controller-first Playden PS5 interface twice: once with RmlFocus and once with direct ps5-homebrew-ui. Use macOS Playden as the reference for visual identity and user journeys. This document maps the whole reference UI, defines the PS5 adaptation, and specifies an interactive comparison workload. It is a reviewable implementation brief; no implementation agents have been dispatched.

The user has reopened the framework choice recorded in the [existing product spec](2026-10-04-playden-ps5-launcher-design.md). This comparison does not select a winner or imply that either stack meets native 4K60. Product additions below are proposed comparison scope, not changes to the connected-library release commitment.

## Comparison definition

Confirmed candidates: RmlFocus and ps5-homebrew-ui. Proposed exact pairing, pending the user's backend clarification: **A = RmlFocus with its RmlUi binding and patched GL3 renderer; B = ps5-homebrew-ui used directly.** RmlFocus also contains a Hui binding; comparing that binding against direct Hui would evaluate framework abstractions over a shared renderer. Do not silently switch A's backend while implementing.

Both agents implement every screen marked **Build** below, including its stated interactions and fixture states. Screens marked **Later** are mapped for product completeness but excluded from this comparison. Items marked **Omit** are macOS-specific or outside the PS5 product direction. No agent may substitute extra polish on a few screens for the agreed coverage.

Use deterministic local services for authentication, library updates, storage, downloads, and game sessions. Interactions must change state and exercise recovery; static screenshots alone do not satisfy the brief. Real Steam authentication, file downloads, Wine execution, cloud synchronization, and OS-wide overlays are separate integration work.

Three possible scopes were considered: a small Library/details sample is quick but misses forms and recovery; a complete live backend port obscures UI costs; a complete agreed UI prototype with simulated services exposes framework strengths and gaps. Use the third approach.

## Reference and evidence

macOS reference root: `/Users/leolobato/Documents/Projetos/Personal/GameNative/playden`, branch `feature/launchers`, clean when inspected at `de952b3d49167e635945ada88cd8533e07d40d44`. All macOS paths below are relative to that root. Read this revision with `git show` or an isolated checkout; do not change the reference checkout.

| Reference | What it establishes |
| --- | --- |
| `App/LibraryModel.swift` | Home/Library/Downloads/Settings tabs; panel inventory; available actions and editing flows |
| `App/LibraryViews.swift`, `App/HomeNavigation.swift`, `App/FocusViews.swift` | App composition, Home shelves, Library grid, game page, focus and scroll behavior |
| `App/DesignSystem.swift`, `docs/design/README.md` | Warm dark palette, Barlow typography, dimensions, focus ring, artwork treatment |
| `App/SetupView.swift`, `App/AuthenticationView.swift`, `App/AccountModel.swift` | Setup and sign-in states, QR challenge and recovery |
| `App/LibraryFilters.swift`, `App/LibraryEditing.swift`, `App/EditingViews.swift` | Filtering, collection editing, confirmations, install offer |
| `App/SupportViews.swift`, `App/DownloadPresentation.swift`, `App/DownloadsStorage.swift`, `App/InstallVolumesPicker.swift` | Downloads, settings, modal routing, keyboard, storage and job actions |
| `App/GameSettingsSheetView.swift`, `App/GameSettingPickerView.swift`, `App/ProfileChooserView.swift`, `App/RuntimeSettingsView.swift` | Nested settings, profile choice, component/status UI |
| `App/UninstallViews.swift`, `App/SessionViews.swift`, `App/NotificationViews.swift`, `App/LogViewer.swift`, `App/ResetView.swift` | Destructive actions, session lifecycle, feedback, logs and reset |
| `App/CloudViews.swift`, `App/StorePage.swift`, `App/ControllerTestView.swift` | Additional reference surfaces; inclusion decided below |
| `docs/images/library.png` | Inspected app Library capture |
| `docs/design/screenshots/1e-gamepage-split-chosen.png`, `2d-downloads.png` | Inspected design boards for detail and queue composition; these are design references, not proof of current runtime behavior |

Additional design boards are in `docs/design/screenshots/`: `1b-home-focusB.png`, `2a-library-grid.png`, `2b-library-filters.png`, `2c-library-search.png`, `1c-library-empty.png`, `2f-firstrun-qr.png`, `2g-firstrun-volume.png`, `2k-settings.png`, `2k-settings-store.png`, `4a-game-settings-tier1.png`, `4b-game-settings-custom-tier2.png`, `4c-picker-graphics.png`, `4d-profile-chooser.png`, `2i-launching.png`, and `2j-exit-overlay.png`. Prefer current source for behavior when an older board differs.

Launcher baseline inspected: `16ea24f8378e282c8bdae2637a1beef3c353d364`. It already includes Home/Library/Settings, details, controller search, bounded library rendering, an install/queue fixture, preferences and diagnostics. `src/app/application.h` and `src/ui/shell.h` depend on RmlUi/RmlFocus; they are not a renderer-neutral application service. `src/library/model.h`, query/cache code and `fixtures/library.json` offer reusable domain groundwork. The install fixture is a small demonstration, not the complete job simulator required here.

Framework baselines inspected:

- RmlFocus: `/Users/leolobato/Documents/Projetos/Personal/GameNative/rmlfocus`, `31d2d8fd39ca3e3bb3ba91a43f50561c04429d57`; also pinned in `dependencies.lock.json`.
- ps5-homebrew-ui: `/Users/leolobato/Documents/Projetos/Personal/GameNative/ps5-homebrew/ps5-homebrew-ui`, `8a716f294835e26a24ef5612f1535aabffef4080`; read `docs/ADOPTING.md` and `docs/COMPONENTS.md`.
- Direct Hui's adoption guide describes desktop OpenGL 4.5+ and limited baked-font coverage. Record host compatibility work and Unicode/font additions explicitly. A Mac host build must be demonstrated rather than assumed; a Linux/Mesa host preview may be an interim route, with the difference reported.

## Navigation map

```text
Start → restore local state → Home
  └─ first run / expired account → Connect Steam → QR → syncing → Home
                                      └─ skip → signed-out Home

Home ───────────────┐
Library ────────────┼→ Game details → More actions / Collections / Rating
  ├─ scope rail    │              ├→ Install offer → Storage picker → Downloads
  ├─ search        │              ├→ Verify / Uninstall → Confirmation
  └─ sort/filter   │              ├→ Game settings → Profile / Setting picker
Downloads ─────────┘              └→ Simulated launch → Running → Return / Quit
  └─ job actions → pause / resume / reorder / cancel / retry / logs
Settings → Account / Library and storage / Appearance / Controller / About
  └─ sign out / reset / quit confirmations
```

Each route owns stable focus identity and viewport state. Dialogs and sheets form a stack above the route; returning from a picker restores its parent sheet, then its original opener. Top-level navigation never discards an ongoing job.

## Whole app screen inventory

IDs are acceptance identifiers used in tests, screenshots and agent reports. Build means an interactive fixture implementation; it does not imply a production service exists.

| ID | Surface | Implementation and essential states | Scope |
| --- | --- | --- | --- |
| U01 | Startup and first run | Restore preferences/cache; visible loading; corrupt/unavailable persistence notice with retry or continue without saving; Connect Steam and Skip. No mandatory runtime setup before browsing. | Build |
| U02 | Steam sign-in | Large QR presentation, account explanation, waiting, approved, expired, refreshing, network failure, denied/cancelled, retry and cancel. A fixture QR is prominently labelled simulated and never presented as a working Steam challenge. Ignore late approval after cancellation. | Build |
| U03 | Account and sync recovery | Signed out, signed in, syncing, expired session, cached/offline, failed refresh and last-sync information. Retry/sign in again keeps navigation recoverable. Account switch cannot show another account's cached data. | Build |
| U04 | Home | Artwork-led horizontal shelves: Continue playing, Downloading, Recently installed, Favorites, pinned collections; omit empty shelves. Fixture history is labelled simulated. Empty Home offers Library/Connect Steam. | Build |
| U05 | Library | Left rail: Installed, All, Favorites, Hidden, collections, New collection. Six-column poster grid, counts, selected-title overlay, install/job/drive badges. Loading, signed out, empty, stale/offline, missing art, refresh while focused. | Build |
| U06 | Search and text entry | Search field and controller keyboard; insert, space, shift, symbols, caret movement, backspace, clear, Done and Cancel. Live count/results; no-results recovery; cancel restores pre-edit text. Reuse editor for collection names and rating notes. | Build |
| U07 | Sort and filters | Right sheet: Name, Recently played, Recently added, Playtime, Install size; installation, genre, controller support, compatibility. Count, selected chips and Reset. Unknown metadata remains unknown. Store/platform selectors are excluded while Steam/Windows is the only target. | Build |
| U08 | Game details | Hero/logo or title fallback; state-dependent primary action; favorite, Game settings, More. Description, genres, source, sizes, play history, compatibility, location summary; missing data shown honestly. Scroll actions and long content without covering hints. | Build |
| U09 | Game actions | Context sheet: favorite, collection membership, hide/unhide, compatibility/note, installation information, verify, uninstall and logs as applicable. Every unavailable action has a reason. Hidden games remain reachable through Hidden. | Build |
| U10 | Collections | Create/name, rename, add/remove game, pin/unpin Home, delete confirmation; empty and duplicate/blank-name validation. Deleting a collection only removes the collection. Stable focus when a rail entry disappears. | Build |
| U11 | Compatibility and notes | Untested, Works, Playable, Broken and optional note; mark as the user's rating. These values never establish PS5 runtime support. | Build |
| U12 | Install offer | Game, destination, known/unknown download and installed bytes, free/reserved space, fetching estimate, ready, insufficient space, unavailable destination, retry, change storage, confirm/cancel. Submission creates exactly one job. | Build |
| U13 | Storage picker and manager | Named volumes, capacity/used/reserved/free, writable/readonly/disconnected state, default selection. Recheck availability before confirming. Never invent a writable PS5 path from a fixture label. | Build |
| U14 | Downloads | Active card, queued items, recent success/failure, storage summary; byte progress, optional speed/ETA, stages and global mini-progress. Empty queue, queued, running, pausing, paused, verifying, completed, failed, cancelled and interrupted/recovered. | Build |
| U15 | Download actions | Pause/resume, move queued item up/down, cancel confirmation, retry failed stage, open game, logs, dismiss completed entry. Detail, tile and queue show the same state. Preserve selection when items move/disappear. | Build |
| U16 | Verify and uninstall | Verify progress/result/failure; uninstall impact summary, Cancel default, busy/result/failure. Prototype mutates only fixture installation records. PS5 policy preserves saves and unrelated files; do not copy macOS's removal of runtime/local saves. | Build |
| U17 | Game settings | Scrollable grouped rows, current profile, inherited versus custom values, reset one/reset all, nested choice picker, text value, disabled capability and save failure. Use the fixed mock settings below, without implying PS5 runtime support. | Build |
| U18 | Profile chooser | Default and custom fixture profiles; profile summary, selected check, apply/back; custom overrides remain visibly distinct. Community download/rating services are excluded. | Build |
| U19 | Runtime status | Settings-only component/status rows: unavailable, checking, ready, failed and retry; clearly simulated. Runtime unavailable must not block Library or downloads. | Build |
| U20 | Launch and running | Simulated preparation, launching, running, launch failure, return-to-game control and game-exited/crashed feedback. Busy/error states offer a way back. No real process launching. | Build |
| U21 | Session controls | In-app fixture backdrop with Return to game, Quit game, stopping, failed stop and force-stop confirmation. Demonstrates modal behavior; an actual overlay above a game and PS-button interception remain future platform work. | Build |
| U22 | Settings | Account; Library and storage; Appearance with reduced motion; Controller status/test; About with build/framework information, logs, reset and Quit. Detail rows, toggles and nested pickers share components. | Build |
| U23 | Controller diagnostics | Connected/disconnected/reconnected; button and stick visualization from host/pad events, focus recovery and back. Pairing is a console/system task. | Build |
| U24 | Logs and operation errors | Scrollable bounded log viewer, empty/loading/error, job/session context; useful reason and retry/back. Fixture records contain no credentials. No Finder action on PS5. | Build |
| U25 | Sign out, reset and quit | Explicit impact summary and safe default focus. Sign out clears fixture session/account-visible cache; reset clears preferences/organization but preserves simulated installs/saves/history; quit with active jobs explains recovery. Failure stays recoverable. | Build |
| U26 | Notifications and persistent warnings | Completion toast, actionable failure notice, lost connection, storage/persistence warning. No unsolicited focus theft; important errors remain reachable after a toast disappears. | Build |
| U27 | Password, Steam Guard, device-code and web sign-in | Reference authentication variants are mapped; PS5 comparison uses QR and its recovery states only. | Later |
| U28 | Cloud saves | Status, synchronization, conflict choice, recovery copies and destructive-save warnings require a separate PS5 contract. | Later |
| U29 | Additional stores and publisher accounts | Epic/GOG, EA/Rockstar/Ubisoft helper/account flows, source selection and embedded store page. Steam-only comparison excludes these integrations and web content. | Later |
| U30 | Community profiles and first-run feedback | Profile lookup/update, compatibility feedback submission and community banners. Local rating and mock profile UI already covered above. | Later |
| U31 | Local Mac games and folders | Discovery, add/rename/relocate app, watched folders, remove reference, macOS/Windows choice, Show in Finder. | Omit |
| U32 | Mac setup and device control | Bluetooth pairing instructions, CrossOver acquisition/template setup, display selection/fullscreen/immersive mode, preferred audio device and Nintendo button-layout preference. Adapt only relevant console status; do not reproduce desktop controls. | Omit |

## Layout and visual contract

Use the macOS Playden visual identity, not either framework's demo theme. Coordinates below are logical units on a 1920 × 1080 canvas. At 3840 × 2160, rasterize text and final geometry at native resolution; scaling a finished 1080p framebuffer does not qualify. Keep the same composition for host preview and PS5.

| Element | Shared requirement |
| --- | --- |
| Palette | Background `#0E0D0C`, panel `#16140F`, primary text `#F3EFE9`, secondary `#A8A29E`, muted `#6B655F`, accent `#F0863A`; green `#7BBF6A`, amber `#E2B53C`, red `#E05A4F` with text/icon labels |
| Typography | Barlow regular/medium/semibold for body; Barlow Condensed medium/semibold for navigation/headings. Reuse reference TTFs and accompanying OFL notices. Use an explicitly shared fallback font for the Unicode fixture. |
| Safe area | 96 horizontal and 54 vertical; shared top bar at `(96,54)`, size `1728×56`; content starts at y=150; bottom legend at y=986, height 40 |
| Type sizes | Page title 60–72; sheet 44–48; navigation 28–30; body 26; metadata/legend 22–24; badges 16 minimum. Long text wraps or truncates predictably; full selected title is accessible. |
| Library | Rail x=96 width=300; grid x=444 width=1380; six columns, posters `210×315`, gap 24. Reserve clipping margins for focus ring/scale; fixed cell pitch as art loads. |
| Home | Shelves with `213×320` posters and gap 20; preserve each shelf's horizontal focus and vertical position. Background hero treatment is subordinate to readable content. |
| Details | Hero up to `1920×620`, logo/title at left, action row around y=640, description and metadata below. Selected actions and long descriptions remain navigable; no hard clipping to match an old screenshot. |
| Downloads | Queue at left, storage summary at right; full active card plus compact queued/recent rows. Progress also appears consistently in details and bottom mini-indicator. |
| Sheets/dialogs | Filters width 640; game settings width 960; centered confirmations 720–1120 depending on content. Dim backdrop, exclusive focus, visible Back/Cancel, nested picker restoration. |
| Focus | Orange ring 4 units around tiles, 3 around compact controls; selected navigation differs from current focus. Restrained scale up to 1.08 tiles / 1.04 buttons. Focus remains visible with reduced motion and missing art. |
| Motion | 180–280 ms navigation/sheet transitions; reduced motion disables scale/slide/fade movement while keeping focus and state legible. Both candidates use the same selected effect treatment. |
| Artwork | Stable placeholder with title and ID-derived color, cover/hero/logo fit rules, loading and failure fallback. Artwork arrival never changes grid geometry. Use the same offline asset bytes in both builds. |

Do not use raw Unicode characters whose font rendering differs to approximate the four PlayStation face-button glyphs. Supply shared vector geometry or equivalent portable assets. Do not make the comparison depend on Apple SF Symbols.

Blur/glow approximations must be documented. If one candidate cannot reproduce an effect, report the gap before removing it from the common reference. A faster result with fewer effects is not a like-for-like rendering result.

## Input and focus contract

This PS5 mapping deliberately resolves a conflict with macOS Playden, where Options opens filters and Triangle opens context. The existing PS5 PRD specifies Options for game actions.

| Input | Behavior |
| --- | --- |
| D-pad / left stick | Move visible focus; consistent initial delay and repeat; one action per input edge |
| Cross | Activate the focused control |
| Circle | Cancel topmost editing/modal layer, then details, then return to Home; on Home root, no destructive action |
| L1 / R1 | Previous/next primary section, wrapping Home → Library → Downloads → Settings; suspended inside modals/text entry |
| Options | Context actions for focused game/job; no action if the current target has none |
| Triangle | Sort/filter in Library; elsewhere only when explicitly shown in the legend |
| Square | Favorite focused game; in keyboard, Backspace with updated legend |
| L2 / R2 | Page through active grid/list while preserving column where possible |
| Touchpad click | Open Library search when no modal owns input; visible Search control remains available |
| PS button | No required app behavior; console may reserve it. Session controls have a visible in-app entry. |

Host keyboard equivalents: arrows, Enter=Cross, Escape=Circle, Q/E=L1/R1, PageUp/PageDown=L2/R2, O=Options, F=Triangle, X=Square, `/`=Search. While editing text, printable keys insert text rather than trigger app shortcuts. Controller input must remain sufficient for all required flows.

At first Library entry focus the first game, or the empty-state action. Left from the first grid column enters the scope rail; Right restores the last grid item in that scope. Top/bottom edges clamp; horizontal movement never accidentally changes rows. Modal opening suppresses underlying focus effects and input. Cancel is the default for destructive confirmations; signing out, uninstalling or deleting cannot execute from the input that opened the dialog.

Remember scope, query, sort, filters, selected game ID and scroll position on return from details. After a dataset change, retain selected ID if present; otherwise clamp the previous index to the new result count; if empty, focus Clear filters or the empty-state CTA. If a Home shelf disappears, use the same rule for its row and child. Asynchronous completion never resets the active route or steals focus.

## Shared data and simulation contract

Freeze the same fixture package before the two implementations start. Keep existing `fixtures/library.json` intact as the 1,000-game stress dataset; add a small curated dataset and scenarios rather than editing it independently in each branch. The common package belongs under `fixtures/ui-comparison/` and is a preparation deliverable, not an existing file claimed by this document.

The package must contain `catalog.json`, `scenarios.json`, `asset-manifest.json`, and `README.md` defining schema, expected outcomes and SHA-256 hashes. Both agents receive identical files. Copy usable Barlow fonts from the reference and reuse the launcher's eight synthetic posters and hero; distribute the exact same additional art/logo assets if needed. No live CDN requests during comparison runs. Design boards guide composition; fixture captures need not feature the same commercial games.

Use 24 curated games with stable synthetic IDs 900001–900024, titles and varied metadata. Include long Latin names, accents, Greek, Cyrillic, CJK and Arabic strings, missing description, unknown size, no logo and failed cover. The last two scripts are deliberate capability tests: font fallback/shaping deficiencies must be visible in the report, never silently replaced with English. The existing 1,000-game fixture covers a narrower script range.

Minimum presentation records, using native language types rather than a cross-renderer abstraction:

| Record | Required data |
| --- | --- |
| Account | Stable account ID, display name, authentication state, challenge expiry, last sync, cache availability, error/recovery |
| Game | Stable ID, title, description, source, art keys, genres, controller support, optional sizes/dates/playtime |
| User library state | Favorites, hidden IDs, collections and membership, ratings/notes, saved query/filter/sort |
| Installation | Game and volume IDs, availability, installed bytes, independent runtime readiness |
| Job | Stable ID, game ID, kind, queue order, stage, state, completed/total bytes, optional rate/ETA, failure/retry target |
| Volume | ID, label, capacity, used/reserved/free bytes, available/writable flags |
| Profile | Profile ID, summary, default values, user overrides, capability and save status |
| Navigation | Route, detail ID, modal stack, focus key, per-container scroll offsets |

UI emits actions; fixture services own state transitions. Reuse existing query/cache code where applicable, but do not force B to link RmlUi through `Application` or `Shell`. No shared UI/rendering wrapper is required. UI framework objects stay out of fixture records and persistent formats.

Fixed mock game settings: Resolution (1280×720 / 1920×1080 / 3840×2160), Frame limit (30 / 60 / Unlimited), VSync toggle, Launch arguments text, and a disabled Graphics backend row with an explanation. Default profile = 1920×1080, 60, VSync on, empty arguments. Custom profile = those defaults plus the user's overrides. These are UI test values, not promised runtime capabilities.

Use one active transfer and two queued jobs in the default download scenario; active download begins at 43%. A manual fixture clock advances work, and screenshot mode freezes it. States: queued → downloading → verifying → downloaded; pause/resume, failure/retry and cancellation are explicit branches. Downloaded is separate from runtime-ready. Restart restores paused/interrupted jobs without inventing completion. All simulation screens carry a unobtrusive but visible “UI simulation” label.

Required injected scenarios: signed-out; QR waiting/expiry/failure/approval; late approval after cancel; library syncing/empty/offline/no results; delayed and failed art; install ready/unknown size/insufficient space; storage disconnect before confirm and during transfer; pause/resume/cancel/retry; verification failure; restart recovery; save failure; runtime unavailable; launch failure/crash; controller disconnect/reconnect.

Persist only into an isolated comparison profile per candidate. Reset scenario state independently of real launcher accounts and files. Candidate A/B state file formats may differ; logical starting states and outcomes may not.

## Components and framework work to account for

| Component family | Required capabilities |
| --- | --- |
| App shell | Tabs, status/account cluster, footer legends, route and modal stacks |
| Collections | Bounded/virtualized poster grid, horizontal shelves, scope rail, focus-driven scrolling and identity restoration |
| Artwork | Asynchronous decode/load, bounded cache/uploads, fit/fill, placeholder, gradient and alpha logo |
| Controls | Text/icon buttons, toggle, chips, radio/check indicators, progress/spinner, metadata and status badges |
| Forms | Text input/keyboard/caret, validation, choice picker, editable settings and nested dialogs |
| Jobs | Progress cards, queue rows, storage bars, live state updates and cancellation/retry |
| Feedback | Toasts, persistent warnings, confirmations, actionable error messages and scrollable logs |
| Platform | Controller sampling/repeat, host keyboard, native-size surface, storage adapter, clean lifecycle and diagnostics |

For each family report: existing framework capability used, Playden-specific code, framework changes, and remaining limitations. B may use the kit's public components directly. A should exercise RmlFocus's real controls/focus/navigation rather than replace the whole app with custom raw drawing. Do not hide missing framework features behind screenshot-only special cases.

## Implementation sequence for both agents

Complete these in the same order and report each checkpoint. Choose framework-specific files and native architecture after inspecting the assigned stack; the shared behavior and fixtures are fixed here.

1. **Freeze inputs.** Record baseline commit, exact framework/backend, fixture hashes, art/fonts and build environment. Prepare the shared fixture package once and give both branches the identical commit/package before UI work. Read each repository's applicable instructions.
2. **Shell and component foundation.** Implement U01 and shared chrome, theme, input, routing, focus, reusable controls, modal stack and fixture selection. Demonstrate keyboard and controller operation.
3. **Browse and organize.** Implement U04–U11 with curated and 1,000-game datasets. Prove detail return, editing cancel and focus reconciliation under refresh before adding management flows.
4. **Account and settings.** Implement U02–U03 and U22–U26, including preference persistence, offline/error recovery and controller diagnostics.
5. **Manage games.** Implement U12–U19 with deterministic storage/job/profile services. Prove nested modal return, job consistency, failure injection and restart recovery.
6. **Session presentation.** Implement U20–U21 using only simulated sessions; provide launch failure, running, quit, failed stop and crash cases.
7. **Capture and report.** Run the shared acceptance tour, save screenshots and a navigation recording, document all missing behavior and framework work. Build/package for PS5 and coordinate console access before running it.

Each candidate lives in a separate worktree/branch from the same launcher baseline. Framework changes belong in separate candidate-specific dependency checkouts with pinned commits or recorded patches. Never modify a dependency checkout another agent is building against. Do not merge either candidate as the selected implementation during this experiment.

## Acceptance tour and evidence

Expose a repeatable entry point for selecting a scenario and replaying logical controller actions. Each implementation documents its exact command in its results README. A fresh profile, fixed clock, fixed assets and fixed fixture version must reproduce each capture. Save route/focused ID/selection/job state assertions alongside visual captures; a picture cannot prove navigation correctness.

| Check | Tour and expected outcome |
| --- | --- |
| T01 | First run → QR → expire → refresh → approve → sync → Home; cancel a second challenge and deliver late success: remain cancelled |
| T02 | Library → several rows down → details → More → close → Back: exact game and viewport restored; repeat from Home shelf |
| T03 | Search via controller, edit/cancel, commit query, no results, Clear; sort/filter/reset and remove focused item: predictable focus, correct count |
| T04 | Favorite, hide/unhide, create/rename/pin/delete collection, rate and edit note; restart: state restored, no game removed by collection deletion |
| T05 | Install → insufficient space → choose writable volume → confirm; disconnect destination before confirmation: installation blocked with recovery |
| T06 | Queue → pause/resume/reorder → cancel and decline, then confirm → inject failure → retry → verify → complete; all surfaces agree |
| T07 | Restart interrupted job, reconnect storage, fail verification; downloaded game with unavailable runtime shows that distinction |
| T08 | Game settings → choice picker → text editor → cancel/commit → reset override → profile chooser → back; each opener regains focus |
| T09 | Verify/uninstall → cancel and confirm; only selected fixture install removed, saves preserved; sign-out/reset summaries match actual fixture mutations |
| T10 | Simulated launch → error/retry → running → return/quit → stop failure; crash notice opens logs and returns to prior details |
| T11 | Toggle reduced motion and restart; inject save failure; disconnect/reconnect controller in a modal; no invisible focus or unreachable recovery |
| T12 | Traverse the 1,000-game Library while art arrives and a job updates; demonstrate bounded materialized items/cache and no focus loss |

Minimum screenshot set at both 1920×1080 and 3840×2160: QR waiting/error, Home, Library focused/empty/no-results, filters, keyboard, details not-installed/downloaded-runtime-unavailable, context/collection editor, install/storage error, active/failed Downloads, game settings/picker/profile, Settings, uninstall confirmation, logs, simulated launch and session controls. Also capture reduced-motion focus and the multilingual fixture. Use matching selected game, time and state in both candidates.

Save evidence under `results/ui-comparison/<candidate>/`: `README.md` with build/run/replay commands and revisions; `coverage.md` covering U01–U32 and T01–T12; `screenshots/`; navigation recording; test output; framework patch list; and measurements if taken. Coverage states are implemented-and-verified, implemented-unverified, blocked, or excluded-by-brief; every blocked item includes reason and recovery work.

Run meaningful tests for focus restoration, nested dialog routing, cancel isolation, queue transitions, persistence failure and account-scoped state. Build success and a screenshot do not establish these behaviors. No need to test static colors by reproducing their constants in unit tests.

## Comparison criteria

Evaluate equal coverage first, then quality. A candidate with missing required flows is incomplete and cannot win simply because it draws a faster Library screen.

| Criterion | Weight | Evidence |
| --- | ---: | --- |
| Visual fidelity and legibility | 25% | Matching captures; type, art, hierarchy, focus, clipping, TV readability |
| Controller navigation and recovery | 25% | T01–T12, physical pad review, modal/scroll restoration, no focus traps |
| Maintainability and implementation effort | 20% | App/framework code separation, custom widgets, patches, development time, build/debug friction |
| UI state correctness | 20% | Errors, empty/offline states, live jobs, cancel behavior, persistence and Unicode evidence |
| Rendering performance | 10% | Equivalent scene, resolution/effects and target hardware; valid measurements only |

Score each evaluated criterion 0–5 with concrete evidence. Leave unavailable measurements unscored and publish the scored subtotal/available weight; do not turn unknown performance into zero misses or a passing score. Report reuse of existing RmlFocus launcher code separately from new implementation effort so the incumbent's head start is visible.

The [execution ledger](../../engineering/v1-progress.md) records the user's deferral of profiling/MSAA/RSS/soak work. This UI brief preserves that deferral: agents retain diagnostics and record any ordinary-run observations, but do not restart sustained benchmarking as an incidental task. Native 4K60 remains the final product requirement. The earlier proposal of p99 ≤18 ms, missed presentations ≤0.1%, no three-miss burst and no persistent memory growth remains unverified. Screenshot mode is visual evidence only. Current unavailable presentation/RSS measurements remain unknown.

When performance work is resumed, use the same hardware, accepted output mode, dataset, effects, interaction script, warmup and duration for both. Separate CPU/frame/swap intervals from actual displayed-frame counters; record measurement provenance and limitations. A host preview cannot establish console performance.

Only one agent may operate the shared console at a time. Both are builds of the same launcher, whose allocated title ID is `FAKE77031`; package names/results must distinguish candidates. Neither agent may reuse a framework/demo's title ID or concurrently replace the installed launcher. Console scheduling and deployment are a coordinated execution step, not something performed by this documentation task.

## Agent handoffs

Send the complete brief plus the frozen common fixture package to each agent. Confirm the backend pairing before dispatch; the rest of the brief is identical.

**Agent A task:** Implement the Build scope of this brief in an isolated Playden branch using RmlFocus with the agreed binding. If the proposed pairing is accepted, use RmlUi and the patched GL3 renderer pinned above. Reuse the current launcher where it helps, record reused versus new work, and keep backend choices explicit. Follow the seven checkpoints, run the acceptance tour, and deliver the runnable build, captures, coverage and framework-gap report. Do not implement real Steam/Wine operations or independently change shared fixtures.

**Agent B task:** Implement the same Build scope from the same launcher baseline in an isolated branch using ps5-homebrew-ui directly. Use kit controls where available, account for platform/font integration, and do not link RmlFocus/RmlUi presentation into this candidate. Follow the same seven checkpoints, tour and deliverables. Preserve the same product design, fixtures and failure behavior. Do not implement real Steam/Wine operations or independently change shared fixtures.

Both agents should report blockers against requirement IDs, distinguish host verification from PS5 evidence, and leave their work independently runnable. Selection and integration follow the side-by-side review.
