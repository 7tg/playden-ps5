# Foundation to Connected Library Implementation Plan

> **Execution status:** Approved and in progress on `main`, using native subagents through `agent-team:agent-team`. Updated after the user selected the direct ps5-homebrew-ui implementation merged in `3bc1e38`, with fixes/evidence through `703dd1f`. This revision supersedes the original RmlFocus presentation choice and the earlier UI-work pause. The user's 4K performance deferral remains in force.

**Goal:** Deliver a native 4K60 Playden shell, prove representative UI performance and direct Steam connectivity, then combine them into the connected-library MVP.

**Architecture:** The selected C++20 presentation consumes ps5-homebrew-ui directly through `src/homebrew/`. Reuse its existing layouts, navigation, focus, keyboard, and state projections; connect them to production application services without introducing another renderer or rewriting the screens. Network, persistence, metadata, and image decoding run away from rendering and deliver bounded, generation-tagged results. Keep deterministic simulation as an explicit development mode.

**Tech Stack:** C++20 UI/C++17 backend, CMake, direct ps5-homebrew-ui `8a716f294835e26a24ef5612f1535aabffef4080` plus the checked-in macOS/CFF patches, SDL/OpenGL on Mac, and the established PS5 native-title toolchain. Steam uses curl 8.22.0, Mbed TLS 3.6.6, the pinned CA bundle, native resolver/nonblocking adapter, and reduced protobuf codec selected by the passed 3A probe. The old RmlFocus targets remain historical regression/reference paths, not the v1 presentation.

**Spec:** [Playden PS5 PRD](../specs/2026-10-04-playden-ps5-launcher-design.md), with the selected [comparison UI](../specs/2026-10-04-ui-framework-comparison.md) and [post-selection evidence](../../../results/ui-comparison/homebrew-ui/fixes-2026-10-04/README.md). The framework selection recorded here supersedes the original PRD's RmlFocus-specific implementation instructions; product requirements remain.

## Global constraints

- Require native 3840 × 2160 UI rendering at 60 Hz for MVP acceptance. An upscaled 1080p interface does not satisfy this requirement.
- Connect to Steam directly from the PS5 with QR approval on a phone; no desktop companion service.
- Preserve PS5-style navigation and focus restoration.
- Build game-management UI before real installation. The install sheet and changing queue here are performance fixtures, not delivery of the full v2 management feature set.
- Keep Wine integration independent and out of these four milestones.
- Keep product behavior here. Reusable changes for the selected renderer belong in ps5-homebrew-ui; retain the recorded isolated dependency patches until separately integrated upstream. Do not modify RmlFocus to implement new direct-Hui behavior.
- Use `../playden` branch `feature/launchers`, inspected at `de952b3d49167e635945ada88cd8533e07d40d44`, provisionally for the user's requested `feature/launcher`. The exact singular branch was absent locally. Record the resolved revision in the reference audit.
- Read each repository's current `AGENTS.md` before changing it. Do not switch or disturb the macOS reference checkout; use `git show <revision>:<path>`.
- Console builds, uploads, and actual acceptance runs are distinct evidence. Coordinate device use with the Wine effort before running tests.
- New paths below are proposed unless marked implemented. Do not reimplement completed comparison screens or disturb the frozen fixture package. The six old RmlUi WIP files are preserved in stash `1f8f649d6e1430f1898b942845db53db668f17a4`; do not restore them into the selected UI.
- Defer profiling, MSAA experiments, RSS investigation, sustained performance runs, and soaks until the user resumes them. Keep native 3840×2160 output; never report deferred 4K60 acceptance as passed.
- Preserve the selected orange active tabs, wrapped confirmation headings, CFF/Greek font fixes, poster spring motion, reduced-motion behavior, and safe modal focus defaults.

## Review focus

- A refresh removes the focused game: recover to a predictable neighbor, tested in 2A and 4A.
- Old artwork completes after a tile is reused: never display the wrong cover, tested in 2A.
- QR approval arrives after cancellation or replacement: never authenticate the wrong attempt, tested in 3B.
- Sign-out races with sync or restart: never show another account's cached library, tested in 3B and 4A.
- Networking or storage stalls during navigation: remain responsive and report timeout/failure, tested in 2B, 3B, and 4B.

## Current baseline and evidence

- **1A–1B are historical completions** for the RmlFocus consumer; keep their evidence. The selected UI needs its own production storage/lifecycle integration and coordinated native acceptance.
- **2A presentation is superseded by the merged direct-Hui UI.** Existing Home/Library/details, controller keyboard, filtering/collections, account/settings, management/session simulations and recovery flows are reusable. Do not build them again from the old RML file list.
- Selected UI evidence at `3796598`, documented by `703dd1f`: host CTest **11/11**, existing suite **14/14**, eight native-resolution **host framebuffer** captures, a 128-frame focus animation, and a signed local PS5 package. These are not PS5 gameplay/display evidence, physical-controller acceptance, or runtime frame-rate measurements.
- **3A passed on PS5** in the earlier native transport probe. **3B foundations are committed** in `da4f925`, `82fd531`, and `5a42a0d`: worker/session storage, protobuf codec/fixtures, and verified HTTP/native networking. Real QR auth and restoration are not implemented yet.
- The selected CMake branch currently returns before creating the backend targets. Its default state, QR, jobs, artwork, account and persistence remain simulation-driven. Building the comparison executable alone does not exercise Steam.

## Order and gates

1. **S1:** make the selected graphical consumer and backend build together, retaining an explicit simulation mode and independent backend tests.
2. **3B:** finish real authentication and wire its bounded events into the existing account/QR surfaces (**4A.1**); then verify direct PS5 phone approval, restore, cancellation, and sign-out. Backend completion need not wait for owned-library retrieval.
3. **3C:** implement authenticated CM/library metadata retrieval; **4A.2** binds those results and account-scoped cache to the existing browsing surfaces.
4. **4A.3 and 4B:** add real asynchronous artwork, validate dynamic title coverage and controller search, then exercise the connected journey and recovery on the actual selected graphical consumer.
5. **2B is deferred.** Keep the final native 4K60 acceptance gate open; functional connected-library work can proceed independently.

The technical lead owns shared contracts, build integration, account-generation rules, console coordination and final verification. Delegate independent backend and presentation-adapter tasks with separate file ownership; sequence consumers after their contracts. Have an independent reviewer inspect meaningful changes, resolve findings, and commit coherent slices on `main`.

## S1 Selected UI and backend build integration

**Files:** Modify `CMakeLists.txt`, `CMakePresets.json`, `cmake/HomebrewUI.cmake`, `cmake/HomebrewPS5.cmake`, `scripts/build-homebrew{,-ps5}.sh`, `dependencies.lock.json`, and `docs/engineering/build.md`; create `cmake/SteamBackend.cmake` and `cmake/PlaydenCore.cmake` if needed to share backend/domain targets between consumers.

**Interfaces:** `playden_storage`, `playden_steam_core`, protocol/transport libraries, and backend test targets must not depend on either renderer. The selected host executable remains `playden-homebrew`; native title ID remains `FAKE77031`. Keep one pinned curl/TLS dependency set per executable and package the CA bundle for the selected consumer.

- [ ] Register the committed protobuf/auth-message/HTTP fixture tests in CTest; build session/service tests under the selected configuration, not only the old `host` target.
- [ ] Move renderer-independent target definitions before presentation selection, and link them into the selected host and native targets. Preserve the verified native resolver/nonblocking behavior and archive/header/CA hash checks; include dependencies in custom link/package rebuild rules.
- [ ] Add selected-UI configure/build/test presets and document current commands. Preserve legacy presets under explicit names; do not silently mistake their successful build for selected-consumer validation.
- [ ] Pin the Hui revision, both patches, header-only image dependency, and supplemental font/license inputs. This task establishes target/link/test/trust-asset integration; production startup and lifecycle checks belong to 4A.1.
- [ ] Build and test the selected host; inspect the graphical executable after integration. Build the selected PS5 package locally. Coordinate any device run first; a build/upload alone is not acceptance.
- [ ] Obtain independent review and commit the reproducible consumer/backend integration.

**Done when:** The selected graphical target links the committed backend foundation and ships its trust assets on both platforms; its test configuration exercises those modules rather than bypassing them. This does not establish real authentication or production startup behavior.

## File ownership for remaining work

| Paths | Responsibility |
| --- | --- |
| `src/homebrew/{view.cpp,render.hpp,input_adapter.hpp,host.cpp,ps5.cpp}` | Selected presentation, input and platform entry points; preserve merged visuals |
| `src/homebrew/{simulation.*,browse.*,storage_sim.*}` | Existing UI state/projection and deterministic simulation; extract only the service boundary needed for production |
| `src/homebrew/controller.{hpp,cpp}` (new) | Production action dispatch and bounded application-event mapping into the existing view state |
| `src/app/library_controller.{h,cpp}` (new), `src/library/` | Account-scoped real library, cache, identity and queries |
| `src/steam/`, `src/steam/backend/` | Authentication worker, credential persistence, HTTP/protobuf/CM adapters |
| `src/homebrew/artwork.{hpp,cpp}` (new) | Visible-key texture subscriptions and UI-thread uploads; background IO/decoding adapter |
| `tests/`, `fixtures/steam/`, `results/` | Behavior checks, synthetic protocol fixtures, sanitized acceptance evidence |

The following completed sections retain the original implementation details as history. Their RmlFocus paths are not instructions to port future work back to RML.

## 1 Build the launcher shell

Implemented and verified 2026-10-04. See [shell evidence](../../../results/20261004T132145Z-shell/README.md). Later milestones are tracked below and in the v1 execution ledger.

### 1A Reproducible graphical consumer

**Files:** Create `CMakeLists.txt`, `CMakePresets.json`, `dependencies.lock.json`, `scripts/build-ps5.sh`, `src/app/application.{h,cpp}`, `src/platform/mac/main.cpp`, `src/platform/ps5/main.cpp`, and `docs/engineering/build.md`.

**References:** `../rmlfocus/docs/design/build.md`, `../rmlfocus/samples/launcher/consumer/`, and the actual `demo/mac/` and `demo/ps5/` front ends. Preserve the patched renderer, native linker/runtime requirements, and system-managed exit path.

**Interfaces:** `Application::Initialize() -> bool`, `Application::Tick(float dt_seconds) -> void`, and `Application::Shutdown() -> void`. Platform entry points own display/input setup and pass semantic commands to the app; application code does not open platform devices.

- [x] Record the exact RmlFocus revision and transitive toolchain/assets needed for reproducibility in `dependencies.lock.json`; document local override paths explicitly.
- [x] Add a `host` configure/build/test preset and an interactive `playden-launcher` target linking the public RmlFocus package. Show a Playden title, focusable control, and controller hints using real rendering.
- [x] Add the PS5 target and a build-only packaging script with an explicitly supplied title ID. Never reuse a framework or runtime title ID by default.
- [x] Verify `cmake --preset host` and `cmake --build --preset host`; launch `build/host/playden-launcher` and inspect input, rendering, and shutdown.
- [x] Verify `scripts/build-ps5.sh --title-id <allocated-id>` produces the title package without contacting the console. Record a separate console launch/navigation/clean-exit result when device access is available.
- [x] Commit the working consumer and its build instructions.

**Done when:** A developer can repeat both builds, run the Mac app, and identify the exact artifacts used for a successful console shell run.

### 1B Navigation, preferences, and diagnostics

**Files:** Create `src/ui/shell.{h,cpp}`, `src/app/preferences.{h,cpp}`, `src/platform/storage.{h,cpp}`, `src/diagnostics/frame_stats.{h,cpp}`, `assets/ui/{home,library,settings}.rml`, `assets/ui/playden.rcss`, `tests/shell_test.cpp`, and `tests/preferences_test.cpp`.

**Interfaces:** `Shell::Handle(const rf::InputEvent&) -> void` consumes the public type in `rmlfocus/input.h`, without introducing a parallel input vocabulary. `Storage::Read(relative_path) -> Result<Bytes>` and `Storage::WriteAtomic(relative_path, bytes) -> Result<void>` own path validation and error reporting. Define these shared `Result`/`Bytes` types in `src/platform/storage.h` before consumers use them.

- [x] Add behavioral tests: L1/R1 moves sections, Circle returns one level, a dismissed sheet restores its opener, and a corrupt preference file restores defaults with a visible diagnostic.
- [x] Run `ctest --preset host --output-on-failure` and confirm the new behavioral assertions fail before implementing the missing behavior.
- [x] Implement Home/Library/Settings navigation using RmlFocus routing; save one preference atomically and restore it on restart. Determine and record writable PS5 paths using console evidence rather than assuming `/data` is accessible.
- [x] Implement frame-start timing and periodic frame/memory records. Capture actual surface/render-target dimensions and refresh configuration; do not infer them from the HDMI output alone.
- [x] Run the host tests to passing, inspect focus on the Mac, and verify preference persistence and clean exit on PS5. Store sanitized evidence under `results/<run-id>-shell/`.
- [x] Commit the shell behavior and diagnostics.

**Done when:** Navigation and persistence survive app restarts, and the app can report trustworthy frame timing for milestone 2.

## 2 Prove representative UI performance

### 2A Library and management performance fixtures

**Files:** Create `src/library/model.h`, `src/ui/{library,details,install_fixture}.{h,cpp}`, `assets/ui/{details,install,confirmation,queue}.rml`, `fixtures/library.json`, `tests/library_navigation_test.cpp`, and `tests/artwork_binding_test.cpp`.

**Interfaces:** Define `GameId` as a stable Steam app ID and `GameSummary` with ID, display name, and artwork source. `LibraryView::SetGames(const std::vector<GameSummary>&) -> void` preserves selection by ID. Fixture management state is explicitly separate from real installation state.

- [x] Create a deterministic 1,000-entry fixture with long names, punctuation, mixed scripts, absent art, and invalid image sources. Use synthetic artwork by default; document provenance for any real assets.
- [x] Add tests that filtering preserves the selected ID where present, removal chooses a neighboring item, nested Back restores each opener, and a late artwork result cannot replace the current tile's cover.
- [x] Run those tests and observe the intended failures; implement bounded visible collections and artwork subscriptions through RmlFocus.
- [x] Add details → install sheet → confirmation and a changing fixture queue. Mark all synthetic operations visibly and keep them unavailable in normal mode.
- [x] Configure native 3840 × 2160 rendering and readable TV scale. Validate font coverage for the fixture names and budget uploads as well as decoding.
- [x] Run `ctest --preset host --output-on-failure` to passing and inspect screenshots of library, long/international names, nested sheets, and queue states.
- [x] Commit the representative scenes and fixtures.

**Done when:** The scenarios exercise the proposed real application workload without depending on Steam or Wine.

### 2B Console measurements and selected-framework improvements — deferred

**Status:** Do not execute this task while the user's performance deferral is active. The existing checker and RmlFocus measurements remain historical; re-establish a selected-Hui baseline when resumed.

**Files:** Reuse `tools/check_frame_report.py`, `tests/test_frame_report.py`, and `docs/engineering/performance.md`; create new `results/<run-id>-4k60/`. Profile the selected ps5-homebrew-ui renderer before proposing reusable framework changes.

**Interfaces:** Export report fields `render_width`, `render_height`, `refresh_hz`, frame intervals, missed presentations, memory samples, scenario markers, and build/dependency revisions. The checker rejects missing data instead of interpreting it as zero.

- [ ] Define acceptance before testing: proposed runs are a 10-minute scripted navigation/artwork/sheet/queue sequence, a 30-minute memory soak, and a physical-controller check. Record loading separately. Proposed interactive gate: frame-interval p99 no greater than 18 ms, missed presentations no greater than 0.1%, no burst of three consecutive missed presentations, and no persistent memory growth after caches reach their limits. Treat these as proposed numerical criteria for review; retain the PRD's native 4K60 requirement regardless of tuning.
- [ ] Test the checker with a valid 4K60 trace, a 1080p trace, a 4K30 trace, missing samples, and average-60 traces hiding repeated stalls. Assert that only the acceptable trace passes.
- [ ] Capture an unmodified baseline on PS5 with concurrent fixture artwork and queue updates. Profile CPU layout, GPU/effects/clipping, texture uploads, and presentation separately where tooling supports it; label unavailable measurements.
- [ ] Make the smallest measured improvements in the selected ps5-homebrew-ui dependency. Add focused regression checks for each changed behavior, run its required tests, and update its own evidence/docs.
- [ ] Pin the passing framework revision in Playden, rerun the representative scenario, and inspect the actual 4K captures for text sharpness, clipping, focus, and modal ordering.
- [ ] Commit the reviewed performance report and checker; keep large raw artifacts out of Git if necessary, with a reproducible manifest and artifact location.

**Done when:** Console evidence satisfies the agreed numerical gate and native-resolution requirement. If it fails, keep this milestone open while independent Steam work proceeds; do not downgrade the MVP to 1080p.

## 3 Prove Steam connectivity

### 3A Reference audit and native networking decision

**Files:** Create `docs/engineering/steam-reference.md`, `docs/engineering/steam-backend-decision.md`, and `probes/steam-network/` with its own build target and sanitized results.

**Reference revision:** Inspect `SteamAuth.swift`, `WebAPI.swift`, `CMClient.swift`, `CMTransport.swift`, `CMDeviceIdentity.swift`, `Library.swift`, `Entitlements.swift`, `PICS.swift`, and `TokenStore.swift` under `Packages/SteamKit/Sources/SteamCore/`, plus `App/AccountModel.swift` and `App/LibraryModel.swift`, at the recorded branch revision.

- [x] Map the smallest chain from QR challenge through approval, persistent session, CM logon, entitlement/library retrieval, and metadata. Record Apple dependencies and the source of generated protocol definitions.
- [x] Probe DNS, certificate-validated HTTPS, request cancellation/timeouts, and the required persistent CM transport in native-title context. Never disable certificate checks to make the probe pass.
- [x] Compare retaining portable source with porting the required protocol subset to a PS5-compatible implementation. Select one based on compiler/runtime support, transport evidence, and dependency footprint.
- [x] Record the chosen libraries, versions, exact backend file map, build instructions, and protocol fixture strategy in the decision document before starting 3B. If no choice is viable yet, report the specific failing probe and continue independent UI work.
- [x] Commit the audit and evidence with tokens and account identifiers removed.

**Done when:** There is an evidence-backed path for both authentication HTTPS and the transport required to obtain a real library. A host-only success does not pass this gate.

### 3B QR authentication and account persistence

**Implemented foundation:** `src/steam/service.*`, `session_store.*`, `auth_backend.h`, `backend/{http_client,native_network,proto_wire,auth_messages}.*`, and their tests/fixtures are committed. Worker generation invalidation, late approval/cancel/sign-out races, private-file permissions, and bounded codec/HTTP behavior have host evidence. Continue these implementations; do not recreate them.

**Files:** Extend the existing Steam service/tests as needed; create `src/steam/backend/auth_client.{h,cpp}`, bounded token/JSON helpers, `src/diagnostics/redaction.{h,cpp}`, and `tests/auth_client_test.cpp`. Reuse the committed schema codec and session repository.

**Interfaces:** `SteamService::BeginQrSignIn() -> RequestId`, `Cancel(RequestId) -> void`, `RestoreSession() -> RequestId`, `SignOut() -> void`, and `Pump(size_t max_events) -> std::vector<SteamEvent>`. Define `RequestId`, `SteamEvent`, and an account/session generation in `service.h`. Events cover challenge, awaiting approval, expiry, authenticated, signed out, and failure; secret token material never enters presentation events.

- [ ] Add deterministic protocol/state tests for challenge replacement, expiry, retry, rejected approval, network timeout, cancellation, and approval arriving for a stale request. Assert stale generations cannot establish a session.
- [ ] Add persistence tests for valid restoration, corrupt data, revoked credentials, and sign-out racing with a late success. Assert no logged record contains fixture token strings.
- [ ] Implement `AuthBackend` using `BeginAuthSessionViaQR`, server-paced `PollAuthSessionStatus`, and `GenerateAccessTokenForApp` from the pinned reference. Test expiry, challenge/client-ID replacement, token subject validation, malformed responses, revoked credentials and renewal rotation. Use a lazy worker-owned HTTP client, finite deadlines and interruptible waits. Save through `SessionRepository`; reject stale generations before persistence or publication.
- [ ] Keep challenge URLs private to the active QR presentation and out of snapshots, replay logs and committed evidence. Encode the actual Steam challenge as QR in 4A.1; the comparison renderer's synthetic matrix cannot authenticate. Tests use synthetic secrets and assert their absence from diagnostic output.
- [ ] Run tests to passing. Verify real QR approval, restart restoration, and sign-out on PS5; do not store QR challenge URLs or session secrets in published evidence.
- [ ] Commit the authenticated service and evidence summary.

**Done when:** PS5 establishes and restores its own Steam session, and canceled or stale operations cannot resurrect it.

### 3C Owned library and metadata retrieval

**Files:** Create `src/library/service.{h,cpp}`, `tests/library_sync_test.cpp`, and sanitized protocol fixtures under `fixtures/steam/`; extend the selected Steam backend.

**Interfaces:** `LibraryService::Refresh() -> RequestId` and `Pump(size_t max_events) -> std::vector<LibraryEvent>`. Events carry account generation and stable game IDs, report loading/completion/errors, and distinguish partial metadata from a successful empty library.

- [ ] Add tests for empty ownership, duplicate records, interrupted sync, malformed metadata, non-ASCII names, and account generation changing mid-request.
- [ ] Run tests to confirm failures, then implement the reference branch's applicable entitlement and metadata path without requiring a public Steam profile or a user-supplied Web API key.
- [ ] Verify bounded processing and cancellation. A failed refresh preserves the previous valid library rather than committing a misleading empty result.
- [ ] Run tests to passing and retrieve the user's real owned library on PS5. Compare sanitized counts and selected known entries with the reference; distinguish metadata omissions from missing ownership.
- [ ] Commit the retrieval implementation and evidence summary.

**Done when:** The console obtains real account-owned game data with stable identity and recoverable errors, independent of the graphical library.

## 4 Connect Steam to the selected MVP presentation

### 4A.1 Existing account/QR surfaces backed by real services

**Files:** Create `src/homebrew/controller.{hpp,cpp}`, `tests/homebrew_account_test.cpp`; modify `src/homebrew/{simulation.hpp,simulation.cpp,render.hpp,view.cpp,host.cpp,ps5.cpp}` only at the state/service boundary. Add a pinned QR encoder if the selected kit lacks one.

**Interfaces:** `Controller::Tick()` drains a bounded number of `SteamService::Pump()` events; UI actions issue `BeginQrSignIn()`, `Cancel(RequestId)`, `RestoreSession()`, and `SignOut()`. Carry request ID and account generation into all presentation updates. Refactor `Render::draw(Simulation&, ...)` to consume shared presentation state/projections if needed; both production and simulation must use the same screens and navigation logic.

- [ ] Extract an explicit production/service mode. Normal startup loads no fixture account, catalog, installs, jobs, runtime readiness or synthetic challenges. Scenario/replay/injection controls remain available only in labelled simulation mode with separate persistence.
- [ ] Use the earlier validated `/app0/playden-data` adapter with a fresh availability/private-write check in the selected native entry point; do not treat `/app0/playden-hui-comparison/profile.json` as credential storage. Fail safely on unavailable storage/networking, surface initialization errors, and cancel/join service work before renderer/display shutdown.
- [ ] Bind existing QR waiting/expired/retry/cancel and Settings account actions to real events. Encode a real challenge with a tested decoder round-trip; replace/clear its private buffer and rendered QR on retry, cancellation, expiry and sign-out.
- [ ] Test stale approvals, replacement challenges, late cancellation, restore failures, and storage errors against the view state. Preserve safe modal defaults, opener focus, reduced motion, and controller-only access.
- [ ] Do not restore authentication from simulation `profile.json`. Keep secrets in `SessionStore`; scope real user annotations/settings that belong to an account to that account. Dispatch persistence away from rendering. A failed clear must remain an explicit failure, not a successful sign-out.
- [ ] Build and inspect the actual selected graphical host. After coordination, verify phone approval from a PS5-issued QR, clean restart restoration, cancellation, and sign-out on `FAKE77031`; publish only sanitized status evidence.
- [ ] Review and commit the account integration with remaining native checks explicitly identified.

**Done when:** Existing account screens operate on the real service and PS5 completes 3B acceptance, with no simulation approval path in normal mode.

### 4A.2 Owned data and account-scoped offline recovery

**Files:** Create `src/app/library_controller.{h,cpp}`, `tests/library_controller_test.cpp`; reuse `src/library/cache.*`, `model.h`, `query.*`, and `tests/library_cache_test.cpp`; extend the selected controller/state projection.

**Interfaces:** `LibraryController::Tick()` drains bounded `LibraryService` events. Reuse `LibraryCache::Load(account_id)` / `Save(account_id, snapshot)`. Map `GameSummary` into existing `homebrew::Game` view records by stable Steam app ID; missing metadata stays unknown. Account/session generation accompanies pending sync/cache work.

- [ ] Test loading, genuine empty ownership, metadata omissions, interrupted refresh and failed refresh preserving the prior valid snapshot. A failed request must never publish a successful empty library.
- [ ] Scope cache load/save, favorites, collections, notes and selected identity to the authenticated account. Test account switch/sign-out during disk IO or sync, offline startup and corrupt cache; old account results must not repopulate cleared views.
- [ ] Populate existing Home/Library/details/loading/stale/error surfaces from real results. Preserve focus/scroll by ID across updates and choose a predictable neighbor on removal; retain search/filter state where valid.
- [ ] Treat installation, runtime readiness, compatibility, download size and play history as independent facts. Hide or explain unavailable real operations; keep the merged install/download/session simulation only in development mode. Do not expand v1 into actual installation or Wine.
- [ ] Run selected UI/controller and backend tests, inspect real-data navigation, and verify cache/retry/sign-out on PS5 independently of performance profiling. Review and commit.

**Done when:** The selected UI shows real owned games and correct account-scoped offline data without synthetic operational status.

### 4A.3 Real artwork and dynamic title coverage

**Files:** Create `src/homebrew/artwork.{hpp,cpp}`, `tests/homebrew_artwork_test.cpp`; extend selected view/renderer and build dependency definitions. Reuse the passed TLS adapter for bounded image retrieval.

**Interfaces:** Artwork requests carry account generation, game ID, source identity and a visible-cell subscription key. IO/decoding returns bounded decoded images; only the graphics thread creates/releases textures with the Hui API. Cache and upload work have explicit count/byte limits, placeholders and cancellation.

- [ ] Replace the fixed nine-fixture-texture path in production with asynchronous download/decode, bounded cache/eviction and bounded per-frame upload work. Reject malformed/oversized images before unbounded allocations; failures retain fixed-size placeholders.
- [ ] Test a late image completing after cell reuse, account change, source change, eviction and shutdown; no stale result may replace another game's cover. Keep fixture timing deterministic.
- [ ] Plan font coverage for actual library titles: current CJK/Arabic atlases are fixture subsets, not general Unicode support. Supply bounded additional glyph coverage or an explicit unsupported-glyph fallback. Test real strings beyond the catalog and mixed scripts; preserve the committed CFF and accented-Greek fixes. Arabic shaping/BiDi remains a documented limitation, not a completed capability.
- [ ] Inspect the selected graphical consumer with delayed and failed real artwork at native dimensions. Validate functionality and bounds without restarting deferred profiling/soaks. Review and commit.

**Done when:** Real titles and artwork arrive asynchronously without wrong-image reuse, unbounded allocations or navigation stalls; remaining script limitations are explicit.

### 4B Search, preferences and connected journey acceptance

**Files:** Reuse `src/library/query.*`, selected `src/homebrew/{browse.*,simulation.*,input_adapter.hpp}`, and merged keyboard; add `tests/mvp_journey_test.cpp`, production search/persistence regressions and `docs/engineering/mvp-acceptance.md`.

**Interfaces:** Retain the selected controller action/focus contract. Reuse the existing UTF-8 query semantics where applicable rather than replacing the merged keyboard or maintaining conflicting search behavior.

- [ ] Verify empty query, punctuation, Latin case folding, exact non-Latin substrings, malformed text, confirm/cancel, retained query, stable sorting and no-results recovery against real game records. Only expose filters supported by actual metadata.
- [ ] Persist production presentation preferences through the real platform adapter; test corrupt/failed writes and restart with a visible recovery path. Simulation profiles do not establish real persistence acceptance.
- [ ] Add one controller journey through fresh sign-in, library, details/back, text entry, retry, offline cached restart, account switch and sign-out. Keep the phone as the only external interaction required for QR approval.
- [ ] Run selected UI and backend suites, inspect graphical captures, build/package, then coordinate native lifecycle and physical DualSense verification. Record physical checks separately from injected input and host replays.
- [ ] **Deferred:** repeat the agreed native 4K60 scenario with real library/artwork, transport delay and storage failure only when the user resumes performance work. Keep the MVP acceptance gate open until required measurements exist.
- [ ] Record revisions, results, pending hardware checks and limitations; review and commit. Do not claim complete MVP acceptance while real Steam, physical-controller or required performance evidence is absent.

## Scope after these tasks

The merged comparison already supplies broad v2 management **simulation** surfaces and behavioral evidence. Preserve and reuse them; do not schedule a duplicate UI build. A later plan can productize those simulations, real installation, or runtime integration separately. Their presence does not authorize v1 to download games, uninstall files, or launch Wine.

## Plan review

This revision retains historical shell/probe evidence, adopts the user's selected direct-Hui UI, records the committed backend foundation, and sequences the remaining service integration. User authorization to resume and commit on `main` persists. 4K performance work remains deferred; successful host captures and local packages are never promoted to console acceptance.
