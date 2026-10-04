# Foundation to Connected Library Implementation Plan

> **For agentic workers:** Use `superpowers:executing-plans` for inline execution, or `superpowers:subagent-driven-development` if the user selects delegation. Execute task by task; this document does not authorize starting implementation.

**Goal:** Deliver a native 4K60 Playden shell, prove representative UI performance and direct Steam connectivity, then combine them into the connected-library MVP.

**Architecture:** A C++ presentation shell consumes RmlFocus through its public package and owns Playden documents and state. Platform and Steam adapters run blocking work away from rendering and deliver bounded events. Steam implementation selection is gated by a portability and console networking probe before writing its production adapter.

**Tech Stack:** C++, CMake, RmlFocus with its pinned RmlUi and patched GL renderer, SDL for Mac development, and the established PS5 native-title toolchain. Steam transport/protobuf dependencies are selected in task 3A; this plan does not assume Swift is available on PS5.

**Spec:** [Playden PS5 PRD](../specs/2026-10-04-playden-ps5-launcher-design.md).

## Global constraints

- Require native 3840 × 2160 UI rendering at 60 Hz for MVP acceptance. An upscaled 1080p interface does not satisfy this requirement.
- Connect to Steam directly from the PS5 with QR approval on a phone; no desktop companion service.
- Preserve PS5-style navigation and focus restoration.
- Build game-management UI before real installation. The install sheet and changing queue here are performance fixtures, not delivery of the full v2 management feature set.
- Keep Wine integration independent and out of these four milestones.
- Keep reusable rendering, focus, and control changes in RmlFocus; keep product behavior here.
- Use `../playden` branch `feature/launchers`, inspected at `de952b3d49167e635945ada88cd8533e07d40d44`, provisionally for the user's requested `feature/launcher`. The exact singular branch was absent locally. Record the resolved revision in the reference audit.
- Read each repository's current `AGENTS.md` before changing it. Do not switch or disturb the macOS reference checkout; use `git show <revision>:<path>`.
- Console builds, uploads, and actual acceptance runs are distinct evidence. Coordinate device use with the Wine effort before running tests.
- New paths below are proposed files to create, not claims that code or commands already exist.

## Review focus

- A refresh removes the focused game: recover to a predictable neighbor, tested in 2A and 4A.
- Old artwork completes after a tile is reused: never display the wrong cover, tested in 2A.
- QR approval arrives after cancellation or replacement: never authenticate the wrong attempt, tested in 3B.
- Sign-out races with sync or restart: never show another account's cached library, tested in 3B and 4A.
- Networking or storage stalls during navigation: remain responsive and report timeout/failure, tested in 2B, 3B, and 4B.

## Order and gates

1. Complete **1A–1B**: reproducible shell and lifecycle.
2. Complete **2A–2B**: representative screens and measured 4K60. Investigate **3A** independently once native packaging is available; Steam feasibility need not wait for all visual polish.
3. Complete **3B–3C**: real QR authentication and library acquisition.
4. Complete **4A–4B**: real-data integration and MVP acceptance.

Each task ends with its stated checks and a focused commit. Commit launcher and RmlFocus changes separately; update the launcher's dependency pin only after the framework change passes its checks. No time estimates are implied.

## Proposed file map

| Paths | Responsibility |
| --- | --- |
| `CMakeLists.txt`, `CMakePresets.json`, `cmake/`, `dependencies.lock.json`, `scripts/` | Reproducible host/PS5 builds and pinned dependencies |
| `src/app/application.{h,cpp}`, `src/platform/{mac,ps5}/main.cpp` | Shared app lifecycle and platform entry points |
| `src/platform/storage.{h,cpp}`, `src/app/preferences.{h,cpp}` | Persistent settings behind platform-owned paths |
| `src/ui/shell.{h,cpp}`, `assets/ui/` | Playden routing, documents, styling, and event binding |
| `src/library/{model,service,cache}.{h,cpp}` | Stable game records, synchronization, and account-scoped cache |
| `src/steam/{service,session_store}.{h,cpp}` | Presentation-facing Steam contract and session persistence |
| `src/steam/backend/` | Production protocol adapter selected in 3A |
| `src/diagnostics/{frame_stats,redaction}.{h,cpp}` | Performance records and credential-safe diagnostics |
| `tests/`, `fixtures/`, `tools/` | Behavioral tests, synthetic data, repeatable runs, and report checking |
| `docs/engineering/`, `results/` | Decisions and reviewed evidence; no credentials or private account dumps |

Do not import the RmlFocus fixture launcher as Playden's domain model. Its installed consumer demonstrates linking but uses a null renderer; it is not the graphical application bootstrap. Use the actual Mac/PS5 front ends as lifecycle references.

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

### 2B Console measurements and RmlFocus improvements

**Files:** Create `tools/check_frame_report.py`, `tests/test_frame_report.py`, `docs/engineering/performance.md`, and `results/<run-id>-4k60/`. Modify RmlFocus files only after profiling identifies the responsible path; inspect its renderer and collection/image implementations rather than preselecting a rewrite.

**Interfaces:** Export report fields `render_width`, `render_height`, `refresh_hz`, frame intervals, missed presentations, memory samples, scenario markers, and build/dependency revisions. The checker rejects missing data instead of interpreting it as zero.

- [ ] Define acceptance before testing: proposed runs are a 10-minute scripted navigation/artwork/sheet/queue sequence, a 30-minute memory soak, and a physical-controller check. Record loading separately. Proposed interactive gate: frame-interval p99 no greater than 18 ms, missed presentations no greater than 0.1%, no burst of three consecutive missed presentations, and no persistent memory growth after caches reach their limits. Treat these as proposed numerical criteria for review; retain the PRD's native 4K60 requirement regardless of tuning.
- [ ] Test the checker with a valid 4K60 trace, a 1080p trace, a 4K30 trace, missing samples, and average-60 traces hiding repeated stalls. Assert that only the acceptable trace passes.
- [ ] Capture an unmodified baseline on PS5 with concurrent fixture artwork and queue updates. Profile CPU layout, GPU/effects/clipping, texture uploads, and presentation separately where tooling supports it; label unavailable measurements.
- [ ] Make the smallest measured improvements in RmlFocus. Add focused regression checks for each changed behavior, run its required tests, and update its own evidence/docs.
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

**Files:** Create `src/steam/service.{h,cpp}`, `src/steam/session_store.{h,cpp}`, `src/diagnostics/redaction.{h,cpp}`, `tests/steam_auth_test.cpp`, and `tests/session_store_test.cpp`; create the backend files selected in 3A.

**Interfaces:** `SteamService::BeginQrSignIn() -> RequestId`, `Cancel(RequestId) -> void`, `RestoreSession() -> RequestId`, `SignOut() -> void`, and `Pump(size_t max_events) -> std::vector<SteamEvent>`. Define `RequestId`, `SteamEvent`, and an account/session generation in `service.h`. Events cover challenge, awaiting approval, expiry, authenticated, signed out, and failure; secret token material never enters presentation events.

- [ ] Add deterministic protocol/state tests for challenge replacement, expiry, retry, rejected approval, network timeout, cancellation, and approval arriving for a stale request. Assert stale generations cannot establish a session.
- [ ] Add persistence tests for valid restoration, corrupt data, revoked credentials, and sign-out racing with a late success. Assert no logged record contains fixture token strings.
- [ ] Run tests to confirm the missing behavior, then implement the chosen backend with finite timeouts and server-directed polling. Save session data through the validated platform adapter and invalidate work when account generation changes.
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

## 4 Connect Steam to the MVP experience

### 4A Real data, artwork, and offline cache

**Files:** Create `src/library/cache.{h,cpp}`, `src/app/library_controller.{h,cpp}`, `assets/ui/signin.rml`, `tests/library_controller_test.cpp`, and `tests/library_cache_test.cpp`; modify the existing shell/library/details views.

**Interfaces:** `LibraryController::Tick() -> void` drains bounded service events and updates views. `LibraryCache::Load(account_id) -> Result<LibrarySnapshot>` and `Save(account_id, snapshot) -> Result<void>` scope records by account; define `LibrarySnapshot` with schema version, retrieval time, and games in `cache.h`.

- [ ] Add tests for cache schema/corruption, offline startup, failed refresh preserving data, account switching/sign-out isolating caches, and refresh/filter changes preserving focus.
- [ ] Run tests to confirm the missing behavior; wire real QR/authentication states into the shell and real library records into the existing views.
- [ ] Load/cache artwork through RmlFocus with fixed card dimensions and placeholders. Keep raw Steam markup and untrusted names as text, never executable UI markup.
- [ ] Add empty/loading/stale/error presentations with retry and cancellation. Display ownership independently of installation/readiness; real games do not inherit fixture install states.
- [ ] Run tests to passing and inspect real-data navigation, artwork arrival, network loss, restart, and sign-out on host fixtures and the console.
- [ ] Commit the integrated connected library.

**Done when:** A signed-in player sees their own library, and offline/retry/account transitions preserve correct identity and responsive navigation.

### 4B Search, settings, and complete MVP acceptance

**Files:** Create `src/library/query.{h,cpp}`, `assets/ui/search.rml`, `tests/library_query_test.cpp`, `tests/mvp_journey_test.cpp`, and `docs/engineering/mvp-acceptance.md`; extend Settings and add reusable keyboard work in RmlFocus if still missing.

**Interfaces:** `QueryLibrary(games, query, sort) -> std::vector<GameId>` operates on cached data. Define query/filter/sort types in `query.h`; MVP filters use available metadata and never imply game compatibility. Controller keyboard integration uses the public RmlFocus control API established by its own implementation.

- [ ] Define and test deterministic title search/sort behavior: empty query, punctuation, case-insensitive Latin matching, exact non-Latin substring matching, and no-results recovery. Do not claim locale-aware collation unless implemented and tested.
- [ ] Implement controller text entry with confirm/cancel, retained search text, and correct focus return. Complete account sign-out/retry and persisted presentation settings.
- [ ] Add a scripted journey covering fresh sign-in states, browsing, details/back, search, refresh failure, cached restart, and sign-out; assert no keyboard/mouse dependency outside phone approval.
- [ ] Run `ctest --preset host --output-on-failure` and the scenario checker to passing. Record physical DualSense acceptance separately from injected input.
- [ ] Repeat the 4K60 scenario with real library data and live artwork; inject transport delay and storage failure. Reopen milestone 2 if integration regresses performance.
- [ ] Record results, limits, revisions, and any remaining failures in the acceptance document. Commit only a passing MVP claim when all required console checks have evidence.

**Done when:** The PRD's connected-library MVP is usable on PS5 at native 4K60 with real Steam connectivity, controller search, persistence, and visible recovery paths.

## Scope after these tasks

The next implementation plan covers complete v2 management UI: deterministic jobs, storage selection, queue control, verification/uninstall flows, and restart recovery. The small management scenes in milestone 2 exist to test rendering and navigation performance; they do not count as completing v2. Real installation and Wine integration remain subsequent, separately specified work.

## Plan review

This plan covers the requested four milestones. The Steam backend is deliberately gated by evidence in 3A, whose output must specify concrete implementation files and dependencies before 3B. Proposed numeric performance criteria require review before hardware acceptance; native 4K60 itself is already agreed. Execution has not started.
