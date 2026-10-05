# prospero-win Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Coordinate runtime changes with its owner; review this draft before implementation.

**Goal:** Install a Steam game with its own prepared Wine bottle and PS5 title, then launch it consistently from Playden and PS5 Home, using Half-Life 2 as the first hardware acceptance case.

**Architecture:** Playden owns downloads, installation records, compatibility recipes, settings and Cloud reconciliation. prospero-win owns the native wrapper, Wine runtime, title-compatible helper and game lifecycle. Bundle immutable runtime assets per game initially, keep mutable bottles/saves separate, and register the title only after preparation succeeds.

**Tech Stack:** Existing C++17 Steam/services and ps5-homebrew-ui; prospero-win's C native wrapper, Wine WoW64, DXVK/RADV; Python host preparation/package tools; PS5 native folder-title registration.

**Spec:** This document's proposed design below, informed by [the runtime owner's report](../../../../ps5-homebrew/prospero-win/docs/FW1360_HALFLIFE2_REPORT.md), [our investigation](../../engineering/prospero-win-investigation.md) and [our launch baseline](../../../results/20261005-hl2-runtime-baseline/README.md). The proposed contract and file names are new work, not existing runtime APIs.

**Status:** Approved by the user on 2026-10-05. Implementation branch: `feat/prospero-win-integration`. Use the patched local Lapy fork described below.

## Evidence and scope

- The runtime owner now verifies HL2 train-station gameplay, audio, left-stick movement, right-stick look, Cross jump, Options menu and keyboard Escape resume on firmware 13.60. The user confirmed physical keyboard movement and working audio. Playden has independently observed rendered gameplay; the control/audio acceptance is the owner's evidence. Saves, sustained frame pacing and long sessions remain unverified.
- Pin the prospero-win wrapper/source baseline to `54caac176e86c60c7c407972583b491f67838ed9` on `feat/fw-v13.60-support`. It includes required native startup fix `824b4ec960b9ee93365437d7bb063db1d0e44b28`; `54caac1` adds verified configuration documentation only. Keep the tested Wine runtime from `e16ec8f3a19dec4a1162e7554ab73ce41f83bdb9`; rebuilding Wine from the wrapper pin is not a reproduction of that runtime. Audio/controller repairs changed bottle registry state, not Wine code.
- The successful combination uses a rebuilt native launcher and helper (`PS5-Lapy-JB-Daemon` `611fa5f`), a repaired prefix, DXVK 2.6.2 and a 1920×1080 virtual monitor. The report's artifact hashes are the baseline, not a ready-to-distribute installation kit.
- Playden already downloads entitled Windows depots, records installs, reads PICS launch metadata, detects PE32/PE64 and drafts strict profiles. `ProsperoGame` currently defaults to shared `prefix = playden` and emits `graphics = auto`.
- Playden's original LncUtil probe crashed before handoff; later instrumentation found null imports. Task 1 uses the public SystemService path after its exact-title root grant; actual handoff is verified separately from caller return codes.
- Existing Cloud code assumes `state_root + "/prefix"`; it does not sync the runtime's actual bottle. Live UI blocks launch/runtime simulation actions and has no production launch service.

First deliverable: a reproducibly prepared, separately registered HL2 title with a real Play action. Next deliverable: lifecycle, Cloud and safe maintenance. General game compatibility and arbitrary Windows installers are outside this milestone.

## Proposed design and choices

| Approach | Benefits | Cost / limitation |
| --- | --- | --- |
| **One bottle and title per game; runtime bundled (recommended)** | Direct PS5 Home launch; settings and registry isolated; installation can be reproduced independently | Larger disk use; title/helper identity and registration must be generalized |
| One shared prospero-win title, private bottles | Smaller initial integration; reuse current catalog | 16-profile catalog limit; no game-specific Home titles; generic library on exit; still needs handoff repair |
| Per-game titles with shared immutable runtime versions | Home titles and lower disk use | Version references, availability and removal accounting add another dependency; defer until bundled implementation works |

Use prebuilt, validated templates and recipe overlays prepared on Linux with matching Wine. The PS5 copies them; it does not run `wineboot` or redistributable installers. Reuse `pw_prefix.py` conversion and `pw_install.py`'s existing DXVK/recipe logic rather than reimplementing their host setup inside Playden. Do not distribute the partially initialized OrbStack prefix or generic registry-duplication repairs.

### Repository integration: pinned submodule plus binary kits

**Use `third_party/prospero-win` as a Git submodule**, consistent with this project's existing native-tooling/UI dependencies. Pin an upstream committed revision; use the sibling checkout for coordination and development, not as a required absolute-path dependency. Confirm the repository's canonical remote at implementation time and push required upstream commits before updating Playden's submodule pointer. Do not vendor a second Wine fork into Playden.

- **Source/tooling:** The submodule supplies wrapper source, profile parsers, preparation validators and packaging tools. Compile the small portable C profile/argument parsers into a host-only contract-test target. Keep Wine/native PRXs out of the Playden executable; each game runs in its separate native title.
- **Artifacts:** Consume a versioned kit containing the built wrapper/helper, Wine PE/native modules, initialized template and recipe dependencies. Store artifact IDs, source revisions, retrieval locations, SHA-256 hashes and capability/schema versions in `dependencies.lock.json`. The wrapper/helper and deployed Wine build can have distinct source revisions, as in the verified baseline; record each separately.
- **Build boundary:** Normal `scripts/build-homebrew.sh` and PS5 launcher builds compile Playden and its small contract tests. An explicit kit build command invokes upstream tools with the necessary Linux/native environments. Missing kit artifacts disable preparation with a clear message; they do not trigger an implicit Wine build or an unpinned download.
- **Development:** Support an explicit `PLAYDEN_PROSPERO_SOURCE_DIR` override for local upstream work. Default to the submodule; reject mismatched revisions for release builds. Keep private console configuration and generated runtime files out of Git.
- **Upgrades:** Land and test runtime changes upstream, produce and validate a new immutable kit, then update the submodule/lock in one reviewed Playden increment. Existing installs retain their recorded kit version until explicitly migrated; changing the submodule pointer never rewrites their bottles/runtime.

Do not add the helper or Wine dependency repositories as separate Playden submodules initially: prospero-win owns their build relationship, while Playden records the resulting provenance and hashes. A submodule gives reproducible source; the separately verified kit gives reproducible installed runtime assets. Both are required.

**Required Lapy source (user instruction):** Use the patched checkout at `../ps5-homebrew/PS5-Lapy-JB-Daemon`, revision `611fa5f7ed46cdde7094b764168390ab5c4e89f8`, for helper builds. Its configured remote is currently `https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon.git`; do not assume that remote contains our local patch or substitute its published release. Record the patched source revision in the kit lock, and arrange a reachable fork/release containing it before clean-checkout distribution. Build using foundation `9c0b994a048521af6fb84c73ded364504fe250e9` and its matching SDK v0.42, and generate the helper/manifest for each actual target title. The verified `PPSA99995` helper SHA-256 is `e31424f19b1308b31d0b63792ac21068ee332c45a49401371bcdbc4a89e65806`; helpers rebuilt for other titles require their own hashes and validation.


**Verified source baseline for Task 3:**

| Component | Required revision / preparation |
| --- | --- |
| prospero-win source/submodule | `54caac176e86c60c7c407972583b491f67838ed9` (`feat/fw-v13.60-support`) |
| Required wrapper code fix | `824b4ec960b9ee93365437d7bb063db1d0e44b28` (included in the source pin) |
| Patched Lapy base | `611fa5f7ed46cdde7094b764168390ab5c4e89f8`, [PR #49](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon/pull/49) |
| Helper build foundation | `9c0b994a048521af6fb84c73ded364504fe250e9`, SDK v0.42 |
| Tested Wine runtime | `e16ec8f3a19dec4a1162e7554ab73ce41f83bdb9`; retain existing verified runtime artifacts |
| Bottle preparation | Restore and validate 32-bit MMDeviceEnumerator and DirectInput8 registrations |

These are planned integration pins; the source submodule and kit lock are still
Task 3 work. Replace both the downloaded helper and its matching manifest during
packaging; a rebuilt wrapper alone does not replace them. The Task 1 helper adds
local explicit FAKE-title CLI support on top of the patched Lapy base. Commit and
pin that extension before shipping kits for FAKE game titles; do not label a helper
built from modified sources as an unmodified `611fa5f` artifact.

### Installation identity and storage

Use a durable `install_id` and persistent allocated `title_id`. Steam app ID identifies the game, not the native title. Do not truncate app IDs into title IDs. Reserve `FAKE77031` and `PPSA99995`, check against titles already registered on the console, persist reservations before use, and reuse an install's allocation on retry/reinstall. Task 1 establishes an allowed title range; no arbitrary range is assumed here.

```text
/data/homebrew/<title_id>/
  eboot.bin, sce_sys/, sce_module/
  lapy.elf, lapy-manifest.json       # validated for this title and firmware
  win/wine/                        # complete pinned immutable runtime
  game-launch.json                 # wrapper reads default profile/data root

/data/playden/games/<install_id>/
  library/profiles/game.profile
  library/profiles/profiles.lst     # exactly game.profile
  library/prefixes/game/            # mutable prepared bottle
  library/input/
  cloud/                           # account-aware baseline and backups
  cache/, logs/, session.json

<selected volume games_root>/<app_id>/  # existing Steam game files
```

These are canonical paths after obtaining `/data` access. Playden must have a verified way to provision/access them before this layout is implemented. Keep existing downloaded games in place; adopt HL2 at `/data/homebrew/FAKE77031/games/220` without another download. New bottle state belongs on internal storage initially; game files use existing volume routing. Moving existing Cloud saves requires a backed-up migration.

The proposed `game-launch.json` v1 carries `schema_version`, `install_id`, `title_id`, `app_id`, `data_root`, `default_profile_id`, `runtime_bundle_id`, `recipe_id`, `recipe_version`, and `exit_policy = "exit-title"`. It contains no Steam credentials. Wrapper and package validation must reject unknown schema versions, identity mismatches, missing profiles and paths outside the recorded managed roots.

Maintain a separate runtime registry so old Steam install records still mean “download complete.” Runtime records hold the descriptor, selected launch entry, volume/path, preparation state, effective settings revision, artifact hashes and last failure. States: `downloaded → preparing → registering → ready`, plus `failed`, `unavailable` and `removing`. Only `ready` enables Play. Interrupted preparation stays repairable without losing downloaded files or saves.

### First compatibility recipe

`steam-220-hl2-v1` selects `hl2.exe`, PE32, `runtime = wine-wow64`, `graphics = dxvk`, `dll_overrides = d3d9=n`, `prefix = game`, `[display] desktop = 1920x1080`, and effective arguments `-game hl2 -steam -w 1920 -h 1080 -novid +exec 360controller.cfg +joystick 1`, `[input] mode = xinput`, and `options = escape`. Keep the shipped `steam_api.dll` unchanged, matching the owner's successful run. Do not infer that other Steamworks/DRM games work.

The bundle includes matching 32/64-bit PE module trees, `wowprospero.dll`, CPU registry selection, both cryptography registry views, console drive mappings and Windows user `prospero`. Install matching x32/x64 D3D9 DXVK DLLs into the bottle; `graphics = dxvk` alone is insufficient. Bottle preparation must restore the 32-bit `Software\Classes\Wow6432Node\CLSID` class and `InprocServer32` registrations for MMDeviceEnumerator `{BCDE0395-E52F-467C-8E3D-C4579291692E}` (`mmdevapi.dll`) and DirectInput8 `{25E609E4-B259-11CF-BFC7-444553540000}` (`dinput8.dll`), with matching DLL paths and `ThreadingModel="Both"`. Apply this to the offline prepared template, preserving unrelated sections, and validate it before packaging; do not patch a running bottle. These are preparation requirements, not additional Wine code patches. Pin DXVK 2.6.2 archive SHA-256 `17761876556afd55736cb895d184f5a1c55d43350f1b1e3b129f8d28706d7992`.

Settings precedence: validated recipe defaults → persisted user overrides → effective profile. Preserve required Steam/recipe arguments and serialize user arguments through a tested compatible quoting path. Reject unsupported launchers/child-process requirements with a specific preparation failure. Unknown games remain downloaded and explain that a compatible preparation recipe is needed.

### Launch, exit and Cloud

Launch by the recorded title ID. Default profile selection in the wrapper makes argument-free PS5 Home launch identical to Playden launch. Game exit closes the per-game title; it must not reopen prospero-win's generic catalog. Automatic return-to-Playden is deferred until a return API is proven.

Before handoff, Playden rechecks readiness/volume and resolves Cloud sync or offers an explicit “Play without syncing” choice for offline/network failure. Conflicts and account confirmation require the player's existing resolution flow. Persist pending session information and flush application state before the native launch call. An API success means handoff accepted, not game running.

The wrapper writes game-scoped startup/exit records with a session ID, settings revision and versions. On the next Playden open, reconcile these with pending launches. A missing exit record means interrupted/unknown, not successful exit. Home launches write their own session ID. Sync saves on the next Playden open, after ensuring the title is no longer using the bottle. Home launch uses local saves and has no automatic network sync in v1; show this behavior in game settings.

## Global constraints

- Preserve the selected ps5-homebrew-ui and existing real download/Cloud services; simulation never establishes runtime readiness.
- No console prefix initialization, no in-place shared runtime updates, no automatic game DLL/DRM modifications.
- Bottle separation is configuration isolation, not a security boundary; wrappers obtain broad `/data` access.
- Execute permission must survive deployment on `eboot.bin` and native PRXs. Validate helper hash, protocol, title identity and firmware capability; do not rename `PPSA99995` artifacts and assume they work.
- Claim/coordinate the shared PS5 before testing. Build/upload success is not console acceptance. Keep native 4K60 outside this milestone.
- Keep runtime work upstream in prospero-win/helper repositories. Preserve all existing uncommitted work; pin committed sources and release artifacts before integration acceptance.

## Review focus

1. Power loss after registration but before marking ready: reconcile native registration and journal without a duplicate title or false-ready state (Task 5).
2. Home launch without arguments: correct game/settings, no catalog fallback, explicit missing-bottle error (Task 2).
3. Incomplete template, wrong DLL architecture or mismatched helper: fail preparation before registration (Tasks 2–3).
4. Volume disappears or another title is running during Cloud/maintenance: retain records/saves and block unsafe operations (Tasks 6–8).
5. Existing install and Cloud paths: adopt without redownload and back up saves before changing mappings (Tasks 4 and 7).

## Implementation tasks

### Task 1: Prove native launch and provisioning contracts

**Files:** Modify `src/homebrew/ps5.cpp`; create `src/platform/ps5/title_runtime.{h,cpp}` and `docs/engineering/prospero-title-contract.md`; record results under `results/<timestamp>-prospero-contract/`.

**Interfaces:** Produce a documented contract for initialization, foreground user, launch/registration/query/removal APIs, title allocation namespace and `/data` provisioning. Proposed platform interface: `TitleBackend::Launch(const std::string& title_id) -> TitleResult`; registration/query/removal signatures are finalized from the native API evidence in this task.

- [ ] Inspect SDK and local working Itemzflow/CheatRunner/native installers to establish exact launch ABI, initialization and required module loading. Capture our crash location before changing the probe.
- [x] Repair and run the isolated handoff probe against an already registered title. Verify actual target startup and repeat after launcher restart; record failures separately from return codes.
- [x] Prove Playden can write outside its current sandbox and register/query/remove a disposable title. Identify whether it needs a title-compatible Lapy helper or a supported installer broker. Treat the broker as a new dependency requiring review if needed.
- [x] Test title metadata/registration visibility, permission preservation, and collision detection. Document title reservation rules and unavailable-provider behavior.
- [x] Commit the platform adapter and evidence as a separate increment. **Gate:** do not start automated installation until native launch, provisioning and registration have a demonstrated supported path.

Task 1 audit: ABI/module inspection is complete, but no original crash backtrace
was retained; the first checkbox remains open for that evidence gap. Native handoff
and external provisioning/registration/query are demonstrated. Removal completed
through the existing console path after closing Playden (native EBUSY while active).
The tiny disposable executable failed module loading separately from registration.
See [the measured contract](../../engineering/prospero-title-contract.md) and
[evidence](../../../results/20261005-prospero-contract/README.md). No unattended
installer/removal or production Play action is claimed from these diagnostics.

### Task 2: Add per-game wrapper mode upstream

**Files (prospero-win):** Modify `native/wine64_main.c`, `tools/build_native.sh`, `tools/package_release.sh`; create `src/pw_title_launch.{h,c}`, `tests/test_pw_title_launch.c`; extend `tests/test_title_identity.py` and `tests/test_package_release.py`. Coordinate helper changes in `../PS5-Lapy-JB-Daemon` if its identity contract requires them.

**Interfaces:** `pw_title_launch_parse(bytes, length, out)` parses the v1 descriptor. The wrapper consumes its title identity, data root, default profile and exit policy; runtime roots must follow the actual title rather than hardcoded `PPSA99995` sandbox paths.

- [ ] Add failing parser/package tests for valid descriptors, unknown schema, unsafe roots, title mismatch, absent default profile and helper mismatch.
- [ ] Add explicit title ID and local pinned helper+manifest build inputs; remove the integration build's dependence on downloading an unfixed “latest” helper. Parameterize metadata and package output paths.
- [ ] Read the descriptor from the title root before elevation changes `/app0` visibility; resolve the supplied data root after elevation. Load exactly its default profile on argument-free startup. Preserve generic prospero-win launcher behavior when no per-game descriptor is supplied.
- [ ] Exit the title after the game ends and write atomic startup/exit status; test that no relaunch/catalog cycle occurs. Report invalid descriptors as startup failures rather than silently falling back to another game.
- [ ] Run upstream profile/launch/identity/package tests and prove a disposable per-game title starts directly from Home on firmware 13.60. Commit and pin the upstream increment.

### Task 3: Produce a reproducible runtime/template kit

**Files (prospero-win):** Create `tools/package_playden_kit.py` and `tests/test_playden_kit.py`; reuse `tools/validate_wine_runtime.py`, `tools/pw_prefix.py` and `tools/pw_install.py`.
**Files (Playden):** Add `.gitmodules` entry and `third_party/prospero-win` gitlink; create `runtime/recipes/steam-220-hl2-v1.json`, `runtime/kits/README.md`, `scripts/build-prospero-kit.sh` and `cmake/ProsperoRuntime.cmake`; update `dependencies.lock.json` with committed wrapper/helper sources and kit manifest hash. Wire portable parser contract tests through `cmake/HomebrewUI.cmake`.

**Interfaces:** Kit manifest v1 lists runtime/template/recipe identity, source revisions, supported firmware/architecture, per-file hashes/modes and capabilities. Input is a complete matching host-prepared prefix; output is a console-ready template and immutable runtime, without game files or personal saves.

- [ ] Test rejection of empty `syswow64`, absent cryptography provider registration, wrong CPU selection, missing 32-bit MMDeviceEnumerator/DirectInput8 COM registrations, missing/mismatched DXVK DLLs, helper mismatch, unsafe archive paths and omitted executable permissions.
- [ ] Build/initialize using a matching Linux Wine environment that completes cleanly. Validate console symlink representation and drive mappings; fail incomplete preparation rather than filling it with ad hoc copies.
- [ ] Add the pinned source submodule, explicit kit build entry point and source override validation. Verify a clean submodule checkout can run the portable parser tests and launcher builds without building Wine; verify a release build rejects an override at the wrong revision.
- [ ] Produce the pinned HL2 kit and record hashes, size and disk requirements including staging overhead. Inspect it for host paths, credentials, user saves and developer log destinations; include dependency licenses/source provenance.
- [ ] Verify the recipe emits the owner's effective HL2 configuration and starts from a fresh bottle on the console. Commit kit tooling/manifest/recipe; keep large generated artifacts outside Git with a documented verified source.

### Task 4: Persist runtime identity and generate effective profiles

**Files:** Create `src/runtime/game_install.{h,cpp}`, `src/runtime/game_settings.{h,cpp}` and `tests/runtime_game_install_test.cpp`; modify `src/runtime/prospero_profile.{h,cpp}`, `tests/prospero_profile_test.cpp`, `cmake/HomebrewUI.cmake` and `cmake/HomebrewPS5.cmake`.

**Interfaces:** `RuntimeInstall` stores the v1 descriptor plus preparation/settings state. `RuntimeInstallRepository::Load/Save` atomically persists records and title reservations. `BuildEffectiveSettings(recipe, overrides)` returns validated settings; extend `ProsperoGame` to consume them with an explicit bottle identifier.

- [ ] Add failing tests for reservations/collisions, restart/reinstall identity, legacy installs without runtime records, malformed records and unsupported kit versions. Import existing HL2 as `downloaded`, retaining its path and Steam install record.
- [ ] Test exact HL2 profile output: `prefix = game`, DXVK/D3D9 override, display and required arguments. Cover quotes/backslashes/spaces, runtime argument-count/storage limits, and rejected paths/overlong values.
- [ ] Implement recipe/user override persistence and atomic profile publication into a one-entry catalog. Settings reset removes overrides without deleting the bottle.
- [ ] Validate generated profiles using pinned upstream `pw_game_profile_parse` **and** the runtime argument splitter in host contract tests; Playden's current Windows argument parser and PW's splitter are not assumed equivalent.
- [ ] Run focused host tests and package compile/link; commit the record/settings/profile increment.

### Task 5: Prepare and register installations transactionally

**Files:** Create `src/runtime/install_service.{h,cpp}`, `src/runtime/package_installer.{h,cpp}`, `tests/runtime_install_service_test.cpp`, `tests/runtime_package_installer_test.cpp`; consume Task 1's adapter; modify `src/homebrew/controller.{hpp,cpp}`, `src/steam/install_service.{h,cpp}` only as needed to observe download completion, and selected CMake registration.

**Interfaces:** `RuntimeInstallService::Prepare(install_id)`, `Retry(install_id)`, `Snapshot()`; a worker consumes downloaded game records + kit and produces preparation states. The Steam download service retains its existing completion meaning.

- [ ] Add failure-injection tests at template copy, hash verification, profile publication, registration and ready-record write. Restart after each boundary must resume/reconcile without duplicate registration or a false Play action.
- [ ] Stage kit/bottle/title, validate free space and all hashes/modes, publish the descriptor/profile, then register. Confirm registration before committing `ready`. Preserve downloaded game files on preparation failure/cancel.
- [ ] Reconcile already registered owned titles against descriptor/install ID; reject unrelated collisions. Clean only owned incomplete staging files and never overwrite a mutable bottle during retry.
- [ ] Project `Preparing compatibility`, `Registering game`, `Ready` and actionable failure/retry into Details/Downloads. Do not label unsupported games ready.
- [ ] Run host service tests and a console adoption of existing HL2; commit with evidence that no redownload occurred.

### Task 6: Connect the real Play action

**Files:** Create `src/runtime/launch_service.{h,cpp}`, `tests/runtime_launch_service_test.cpp`; modify `src/homebrew/controller.{hpp,cpp}`, `src/homebrew/simulation.{hpp,cpp}`, `src/homebrew/host.cpp`, `src/homebrew/ps5.cpp`; extend selected model/controller tests and CMake targets.

**Interfaces:** `LaunchService::Request(install_id)` and `Snapshot()` perform readiness checks and emit a main-thread handoff request; `TitleBackend::Launch(title_id)` performs the platform call. Host tests inject a fake backend; normal macOS builds report native launch unavailable.

- [ ] Test missing volume/bottle/runtime/helper, duplicate Play requests, changed settings during preparation and failed native launch. No invalid input may invoke the title backend.
- [ ] Wire live UI Play requests through the controller instead of the simulation session timer. Show `Preparing`/`Launching` and native failure text; never synthesize `Running` from a timer or successful API return.
- [ ] Flush preferences/settings, persist the pending session and bounded launch log, then call the native adapter on its required thread. Remove the one-shot development request path from normal launch behavior.
- [ ] Hardware-test Playden → HL2 and argument-free Home → HL2 using the same profile, then exit/reopen/retry. Commit the real launch increment.

### Task 7: Bind Cloud to the actual bottle and reconcile sessions

**Files:** Modify `src/steam/cloud_service.h`, `src/steam/backend/steam_cloud_runner.{h,cpp}`, `src/homebrew/controller.{hpp,cpp}` and launch service; create `src/runtime/session_reconciler.{h,cpp}`, `tests/runtime_session_reconciler_test.cpp`; extend `tests/steam_cloud_test.cpp` and `tests/cloud_service_test.cpp`.

**Interfaces:** Add explicit `prefix_root` to `CloudLocation`, retaining a legacy fallback for unprepared installs. `SessionReconciler::Reconcile(records, title_status)` queues an account-aware Cloud check only when the game is no longer active; it does not require Playden to have survived handoff.

- [ ] Test runtime saves map to `library/prefixes/game`, with state/baseline at `cloud/`; migration backs up existing `state_root/prefix` saves and refuses ambiguous differing copies without a choice.
- [ ] Test prelaunch conflict/account confirmation, offline “Play without syncing”, changed account during sync and launch cancellation. Native handoff waits for the chosen resolution.
- [ ] Test clean exit, Home-originated session, crash/missing exit record, duplicate reconciliation and active title. Never upload from an active bottle or assume a missing status means clean completion.
- [ ] Implement per-session next-open reconciliation with persisted completion, backup/conflict behavior and no credentials in wrapper files. Run Cloud/model tests and controlled console save/load/sync acceptance; commit.

### Task 8: Settings, repair, update and uninstall

**Files:** Modify `src/homebrew/simulation.{hpp,cpp}`, `src/homebrew/view.cpp`, runtime settings/install services and package installer; create `tests/runtime_maintenance_test.cpp`; extend graphical management/settings tests.

**Interfaces:** `Repair(install_id)` revalidates assets/recipe without replacing saves; `Remove(install_id, remove_saves)` unregisters owned title/files while defaulting to save retention; updates stage a new pinned kit and retain the previous manifest for rollback.

- [ ] Expose preset/reset, launch entry/arguments, validated resolution, controller preset and FPS overlay; put supported graphics/runtime/DLL/log choices in advanced settings. Hide unsupported frame-limit/VSync mappings, DXVK 120 Hz and unvalidated scaling options. HL2 input defaults remain marked unverified until tested.
- [ ] Test settings persistence and identical Home/Playden effective profiles; repair failure retains saves and user overrides. Back up game-root saves before deleting downloaded game files.
- [ ] Test interrupted uninstall, failed unregister, unavailable volume and running game; resume owned cleanup safely while preserving the allocation until unregister completes. Default uninstall retains bottle/saves/Cloud state; deleting saves is a separate explicit choice.
- [ ] Stage updates without modifying active runtime assets; block update/reset while running. If a recipe migrates the bottle, back it up and include it in rollback; otherwise preserve the bottle. Test failed update restores old runtime/settings and launchability.
- [ ] Run host maintenance/graphical tests and console repair/reinstall/save-retention checks; commit and update status/evidence.

## Verification and release gates

For each task, run newly added focused tests first, then the selected host build/suite when the increment is complete. Register tests in the selected CMake targets. Run legacy checks only for changes to shared code; run upstream C/Python contract suites for wrapper/kit changes. A failing hardware gate leaves the corresponding milestone open.

```sh
cmake --build --preset homebrew-host -j 4
ctest --preset homebrew-host --output-on-failure
scripts/build-homebrew-ps5.sh
git diff --check
```

Final console acceptance: fresh validated bottle; launch from Playden and Home; start a level; movement/look/actions/menu controls; audio; save, close and load after restart; Cloud reconciliation and conflicts; unavailable volume recovery; repair/update rollback/uninstall save retention. Record HDMI evidence and startup/helper/Wine logs with exact artifact versions. Run at least 20 minutes of gameplay and report measured pacing/crashes separately from the existing menu FPS overlay; no blanket 60 fps claim.

## Decisions for this review

1. Approve the recommended per-game title/bottle and bundled-runtime model, with shared runtime deduplication deferred.
   Use a pinned `third_party/prospero-win` source submodule and separately versioned, hash-verified binary/template kits.
2. Approve prebuilt validated compatibility kits and curated recipes as the first preparation mechanism; generic host-dependent installers remain unsupported.
3. Approve exit-to-Home plus next-open Cloud reconciliation; Home launches initially use local saves.
4. Approve HL2-only initial acceptance and save-preserving uninstall by default.
5. Resolve platform access/title allocation in Task 1 before committing to automatic native installation. Any required persistent broker gets its own reviewed design.

Recommended order: prove platform contracts → upstream wrapper + reproducible kit → adopt HL2 and enable real launch → Cloud/lifecycle → maintenance and broader recipes. Tasks 1–6 establish the first launch milestone; Tasks 7–8 complete managed runtime integration.
