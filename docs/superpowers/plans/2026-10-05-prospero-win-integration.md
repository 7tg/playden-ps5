# prospero-win Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Coordinate runtime changes with its owner; review this draft before implementation.

**Goal:** Install a Steam game with its own prepared Wine bottle and PS5 title, then launch it consistently from Playden and PS5 Home, using Half-Life 2 as the first hardware acceptance case.

**Architecture:** Playden owns downloads, installation records, compatibility recipes, settings and Cloud reconciliation. prospero-win owns the native wrapper, Wine runtime, title-compatible helper and game lifecycle. Bundle immutable runtime assets per game initially, keep mutable bottles/saves separate, and register the title only after preparation succeeds.

**Tech Stack:** Existing C++17 Steam/services and ps5-homebrew-ui; prospero-win's C native wrapper, Wine WoW64, DXVK/RADV; Python host preparation/package tools; PS5 native folder-title registration.

**Spec:** This document's proposed design below, informed by [the runtime owner's report](../../../../ps5-homebrew/prospero-win/docs/FW1360_HALFLIFE2_REPORT.md), [our investigation](../../engineering/prospero-win-investigation.md) and [our launch baseline](../../../results/20261005-hl2-runtime-baseline/README.md). The proposed contract and file names are new work, not existing runtime APIs.

**Status:** Original design approved on 2026-10-05; Task 1 complete. Remaining-work execution details updated for review on 2026-10-05. Implementation branch: `feat/prospero-win-integration`. Use the patched local Lapy fork described below.

**Interim review:** [Per-game/kit findings, corrections and remaining release gates](../../../results/20261005-per-game-kit-review/README.md). Structural test passes do not close provenance, native glue or fresh-bottle hardware acceptance gates.

**Roadmap:** [Milestones, dependencies and acceptance gates](../../engineering/prospero-win-roadmap.md).

**Settings ownership:** The user assigned game settings, including associated
prospero-win/dependency patches, to `feat/game-settings`. That branch owns the
Task 4 settings model/effective profile writer and Task 8 settings UI. This
integration branch supplies runtime records, bottles/kits, native access and
preparation/launch/lifecycle services. Integrate the owner's settings changes
through their recorded interfaces and parser capability revisions; do not ship
a competing settings model. Our interrupted partial settings files are preserved
only in ignored `build/game-settings-handoff/` for reference. Maintenance and
uninstall lifecycle work remain part of this integration goal.

The owner's worktree is
`/Users/leolobato/.chauffeur/worktrees/18C0FEEF-6A3F-52E4-64F1-24A93D005389/feat-game-settings`.
Its settings model and generated profiles were inspected at `e3cc160`; settings
UI work is already present at `ff3ffa6`. A later inspection at `7937f02` includes
settings persistence, environment-variable controls, community profile sidecars
and shader-cache actions. These are coordination checkpoints,
not approved dependency pins. Read the current branch before integration and
preserve its in-progress work.

Use these interfaces when joining the branches:

| Boundary | Integration requirement |
| --- | --- |
| Settings to profile | Use the owner's `BuildEffectiveSettings` and `RenderProsperoProfile`, followed by the strict prospero parser check. The installation service calls an injected profile publisher; it does not define another settings model. |
| Settings revision | Runtime records and the wrapper descriptor use exact `uint64_t`; the reviewed settings integration now uses exact `uint64_t` for `GameSettingsRecord` and lexical JSON parsing. Revision exhaustion rejects changes. Never truncate or wrap a revision, and make the record, generated profile metadata and descriptor agree. |
| Recipe identity | Convert the owner's positive integer recipe version to a canonical decimal string for the runtime record/descriptor. Keep the recipe ID and selected launch entry consistent across publication. |
| Capabilities | Populate the owner's `KitCapabilities` only from the installed kit's verified parser/runtime capabilities. Defaults remain false for DXVK config, environment forwarding and registry overlays until their implementation and acceptance checks pass. |
| Publication and retry | Publish the effective profile and matching descriptor while the game is stopped. Persist a journal before effects, reconcile interrupted publication on retry, and expose Ready only after ownership, artifact and settings-revision checks pass. Home and Playden must consume the same persisted profile. |
| Source and dependency pins | Combine the tested per-title wrapper changes with the owner's Wine/settings patches in an isolated checkout, review and test the combination, then pin the resulting source commit and kit hashes. Keep the retained Wine runtime until its replacement is independently verified. |

## Evidence and scope

- The runtime owner now verifies HL2 train-station gameplay, audio, left-stick movement, right-stick look, Cross jump, Options menu and keyboard Escape resume on firmware 13.60. The user confirmed physical keyboard movement and working audio. Playden has independently observed rendered gameplay; the control/audio acceptance is the owner's evidence. Saves, sustained frame pacing and long sessions remain unverified.
- Pin the prospero-win wrapper/source baseline to a tested Task 2 descendant of baseline `54caac176e86c60c7c407972583b491f67838ed9` on `feat/fw-v13.60-support`. It includes required native startup fix `824b4ec960b9ee93365437d7bb063db1d0e44b28`; `54caac1` adds verified configuration documentation only. Keep the tested Wine runtime from `e16ec8f3a19dec4a1162e7554ab73ce41f83bdb9`; rebuilding Wine from the wrapper pin is not a reproduction of that runtime. Audio/controller repairs changed bottle registry state, not Wine code.
- The successful combination uses a rebuilt native launcher and helper (`PS5-Lapy-JB-Daemon` `611fa5f`), a repaired prefix, DXVK 2.6.2 and a 1920×1080 virtual monitor. The report's artifact hashes are the baseline, not a ready-to-distribute installation kit.
- Playden already downloads entitled Windows depots, records installs, reads PICS launch metadata, detects PE32/PE64 and drafts strict profiles. `ProsperoGame` currently defaults to shared `prefix = playden` and emits `graphics = auto`.
- Playden's original LncUtil probe crashed before handoff; later instrumentation found null imports. Task 1 uses the public SystemService path after its exact-title root grant; actual handoff is verified separately from caller return codes.
- Existing Cloud code assumes `state_root + "/prefix"`; it does not sync the runtime's actual bottle. Live UI blocks launch/runtime simulation actions and has no production launch service.

First hardware deliverable (M1): a reproducibly prepared, separately registered HL2 title launched directly from Home. First product deliverable (M2): adopt/install HL2 and launch it through a real Play action. Next deliverable: lifecycle, Cloud and safe maintenance. General game compatibility and arbitrary Windows installers are outside this milestone.

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
- **Title variants:** A kit declares an explicit finite set of prebuilt title-specific wrapper/helper pairs. Allocate only a free title ID in the intersection of the managed namespace and the kit's available variants. The PS5 does not build SDK artifacts or rename another title's helper. When all supplied variants are occupied, report that additional variants are required; a new kit can extend the set without changing existing allocations.
- **Build boundary:** Normal `scripts/build-homebrew.sh` and PS5 launcher builds compile Playden and its small contract tests. An explicit kit build command invokes upstream tools with the necessary Linux/native environments. Missing kit artifacts disable preparation with a clear message; they do not trigger an implicit Wine build or an unpinned download.
- **Development:** Support an explicit `PLAYDEN_PROSPERO_SOURCE_DIR` override for local upstream work. Default to the submodule; reject mismatched revisions for release builds. Keep private console configuration and generated runtime files out of Git.
- **Upgrades:** Land and test runtime changes upstream, produce and validate a new immutable kit, then update the submodule/lock in one reviewed Playden increment. Existing installs retain their recorded kit version until explicitly migrated; changing the submodule pointer never rewrites their bottles/runtime.

Do not add the helper or Wine dependency repositories as separate Playden submodules initially: prospero-win owns their build relationship, while Playden records the resulting provenance and hashes. A submodule gives reproducible source; the separately verified kit gives reproducible installed runtime assets. Both are required.

**Required Lapy source (user instruction):** Use the patched checkout at `../ps5-homebrew/PS5-Lapy-JB-Daemon`, revision `611fa5f7ed46cdde7094b764168390ab5c4e89f8`, for helper builds. Its configured remote is currently `https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon.git`; do not assume that remote contains our local patch or substitute its published release. Record the patched source revision in the kit lock, and arrange a reachable fork/release containing it before clean-checkout distribution. Build using foundation `9c0b994a048521af6fb84c73ded364504fe250e9` and its matching SDK v0.42, and generate the helper/manifest for each actual target title. The verified `PPSA99995` helper SHA-256 is `e31424f19b1308b31d0b63792ac21068ee332c45a49401371bcdbc4a89e65806`; helpers rebuilt for other titles require their own hashes and validation.


**Verified source baseline for Task 3:**

| Component | Required revision / preparation |
| --- | --- |
| prospero-win wrapper/source baseline | `54caac176e86c60c7c407972583b491f67838ed9` (`feat/fw-v13.60-support`) |
| Required wrapper code fix | `824b4ec960b9ee93365437d7bb063db1d0e44b28` (included in the source pin) |
| Exact FAKE-title build extension | `e9f2cfc2c3913e42fdf83d74291e270b041ff0d9` (descends from the patched base) |
| Patched Lapy base | `611fa5f7ed46cdde7094b764168390ab5c4e89f8`, [PR #49](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon/pull/49) |
| Helper build foundation | `9c0b994a048521af6fb84c73ded364504fe250e9`, SDK v0.42 |
| Tested Wine runtime | `e16ec8f3a19dec4a1162e7554ab73ce41f83bdb9`; retain existing verified runtime artifacts |
| Bottle preparation | Restore and validate 32-bit MMDeviceEnumerator and DirectInput8 registrations |

These are planned integration pins; the source submodule and kit lock are still
Task 3 work. Task 2 introduces a new wrapper revision; pin its tested descendant
in the submodule/kit lock rather than packaging the unchanged baseline without per-game support. Replace both the downloaded helper and its matching manifest during
packaging; a rebuilt wrapper alone does not replace them. The Task 1 helper adds
explicit FAKE-title CLI support committed as `e9f2cfc2c3913e42fdf83d74291e270b041ff0d9`
on top of the patched Lapy base. Pin that extension before shipping kits for FAKE game titles; do not label a helper
built from modified sources as an unmodified `611fa5f` artifact.

Kit validation also requires an independently reviewed helper SHA-256 pin for
each compiled title variant. Source revision strings supplied by the kit spec
or helper manifest do not establish binary provenance. The current FAKE78001
helper pin is `5608915201204dfcd1846b01dd2a9e60d03aa390c69b1dc61f3b150566bf1935`;
additional title variants require reviewed builds and new pins before a kit can
include them. This hash check does not replace per-title hardware acceptance.

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
  cache/, logs/sessions/<session_id>.json

<selected volume games_root>/<app_id>/  # existing Steam game files
```

These are canonical paths after obtaining `/data` access. Playden must have a verified way to provision/access them before this layout is implemented. Keep existing downloaded games in place; adopt HL2 at `/data/homebrew/FAKE77031/games/220` without another download. New bottle state belongs on internal storage initially; game files use existing volume routing. Moving existing Cloud saves requires a backed-up migration.

The proposed `game-launch.json` v1 carries `schema_version`, `install_id`, `title_id`, `app_id`, `data_root`, `default_profile_id`, `settings_revision`, `runtime_bundle_id`, `recipe_id`, `recipe_version`, and `exit_policy = "exit-title"`. It contains no Steam credentials. Wrapper and package validation must reject unknown schema versions, identity mismatches, missing profiles and paths outside the recorded managed roots.

Maintain a separate runtime registry so old Steam install records still mean “download complete.” Runtime records hold the descriptor, selected launch entry, volume/path, preparation state, effective settings revision, artifact hashes and last failure. States: `downloaded → preparing → registering → ready`, plus `failed`, `unavailable` and `removing`. Only `ready` enables Play. Interrupted preparation stays repairable without losing downloaded files or saves.

### First compatibility recipe

`steam-220-hl2-v1` selects `hl2.exe`, PE32, `runtime = wine-wow64`, `graphics = dxvk`, `dll_overrides = d3d9=n`, `prefix = game`, `[display] desktop = 1920x1080`, and effective arguments `-game hl2 -steam -w 1920 -h 1080 -novid +exec 360controller.cfg +joystick 1`, `[input] mode = xinput`, and `options = escape`. Keep the shipped `steam_api.dll` unchanged, matching the owner's successful run. Do not infer that other Steamworks/DRM games work.

The bundle includes matching 32/64-bit PE module trees, `wowprospero.dll`, CPU registry selection, both cryptography registry views, console drive mappings and Windows user `prospero`. Install matching x32/x64 D3D9 DXVK DLLs into the bottle; `graphics = dxvk` alone is insufficient. Bottle preparation must restore the 32-bit `Software\Classes\Wow6432Node\CLSID` class and `InprocServer32` registrations for MMDeviceEnumerator `{BCDE0395-E52F-467C-8E3D-C4579291692E}` (`mmdevapi.dll`) and DirectInput8 `{25E609E4-B259-11CF-BFC7-444553540000}` (`dinput8.dll`), with matching DLL paths and `ThreadingModel="Both"`. Apply this to the offline prepared template, preserving unrelated sections, and validate it before packaging; do not patch a running bottle. These are preparation requirements, not additional Wine code patches. Pin DXVK 2.6.2 archive SHA-256 `17761876556afd55736cb895d184f5a1c55d43350f1b1e3b129f8d28706d7992`.

Settings precedence: validated recipe defaults → persisted user overrides → effective profile. Preserve required Steam/recipe arguments and serialize user arguments through a tested compatible quoting path. Reject unsupported launchers/child-process requirements with a specific preparation failure. Unknown games remain downloaded and explain that a compatible preparation recipe is needed.

### Launch, exit and Cloud

Launch by the recorded title ID. Default profile selection in the wrapper makes argument-free PS5 Home launch identical to Playden launch. Game exit closes the per-game title; it must not reopen prospero-win's generic catalog. Automatic return-to-Playden is deferred until a return API is proven.

Before handoff, Playden rechecks readiness/volume and resolves Cloud sync or offers an explicit “Play without syncing” choice for offline/network failure. Conflicts and account confirmation require the player's existing resolution flow. Persist pending session information and flush application state before the native launch call. A returned result is not proof of game startup. Task 1 observed `0x80940010` followed by successful ShadowMount handoff; persist the result and reconcile service/wrapper evidence instead of immediately declaring Running or final failure.

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

- [x] Inspect SDK and local working Itemzflow/CheatRunner/native installers to establish exact launch ABI, initialization and required module loading. Capture our crash location before changing the probe.
- [x] Repair and run the isolated handoff probe against an already registered title. Verify actual target startup and repeat after launcher restart; record failures separately from return codes.
- [x] Prove Playden can write outside its current sandbox and register/query/remove a disposable title. Identify whether it needs a title-compatible Lapy helper or a supported installer broker. Treat the broker as a new dependency requiring review if needed.
- [x] Test title metadata/registration visibility, permission preservation, and collision detection. Document title reservation rules and unavailable-provider behavior.
- [x] Commit the platform adapter and evidence as a separate increment. **Gate:** do not start automated installation until native launch, provisioning and registration have a demonstrated supported path.

Task 1 audit: complete. ABI/module inspection and original-artifact fault capture
are verified. The first historical trace was not retained; replay of the unchanged
original executable captured SIGSEGV/RIP zero for FAKE77031 pid535. This replay
occurred after fix development and before restoring the fixed console executable,
a recorded deviation from the requested initial investigation order. Native handoff
and external provisioning/registration/query are demonstrated. Removal completed
through the existing console path after closing Playden (native EBUSY while active).
The tiny disposable executable failed module loading separately from registration.
See [the measured contract](../../engineering/prospero-title-contract.md) and
[evidence](../../../results/20261005-prospero-contract/README.md). No unattended
installer/removal or production Play action is claimed from these diagnostics.

### Task 2: Add per-game wrapper mode upstream

**Files (prospero-win):** Modify `native/wine64_main.c`, `tools/build_native.sh`, `tools/package_release.sh`, `Makefile`; create `src/pw_title_launch.{h,c}`, `tests/test_pw_title_launch.c`; extend `tests/test_title_identity.py` and `tests/test_package_release.py`. Coordinate helper changes in `../PS5-Lapy-JB-Daemon` if its identity contract requires them.

**Interfaces:** `pw_title_launch_parse(bytes, length, expected_title_id, out, error, error_size)` parses the v1 descriptor. The wrapper consumes its title identity, data root, default profile and exit policy; runtime roots must follow the actual title rather than hardcoded `PPSA99995` sandbox paths.

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

### Task 4: Persist runtime identity, establish native access and generate effective profiles

**Files:** Create `src/platform/ps5/native_access.{h,cpp}`, `src/runtime/game_install.{h,cpp}`, `src/runtime/game_settings.{h,cpp}`, `tests/native_access_test.cpp` and `tests/runtime_game_install_test.cpp`; modify `src/homebrew/ps5.cpp`, `src/runtime/prospero_profile.{h,cpp}`, `tests/prospero_profile_test.cpp`, `cmake/HomebrewUI.cmake`, `cmake/HomebrewPS5.cmake` and `scripts/build-homebrew-ps5.sh`.

**Interfaces:** `RuntimeInstall` stores the v1 descriptor plus preparation/settings state. `RuntimeInstallRepository::Load/Save` atomically persists records and title reservations. `BuildEffectiveSettings(recipe, overrides)` returns validated settings; extend `ProsperoGame` to consume them with an explicit bottle identifier.

- [ ] Add failing tests for reservations/collisions, restart/reinstall identity, legacy installs without runtime records, malformed records and unsupported kit versions. Import existing HL2 as `downloaded`, retaining its path and Steam install record.
- [ ] Test exact HL2 profile output: `prefix = game`, DXVK/D3D9 override, display and required arguments. Cover quotes/backslashes/spaces, runtime argument-count/storage limits, and rejected paths/overlong values.
- [ ] Add production native access before UI/services start; resolve title assets, trust store, existing Steam state and logs through stable paths after the grant. On failure retain ordinary launcher browsing/download behavior and disable preparation/launch with a capability error. Verify existing account, artwork and Downloads still work after access succeeds.
- [ ] Implement recipe/user override persistence and atomic profile publication into a one-entry catalog. Settings reset removes overrides without deleting the bottle.
- [ ] Validate generated profiles using pinned upstream `pw_game_profile_parse` **and** the runtime argument splitter in host contract tests; Playden's current Windows argument parser and PW's splitter are not assumed equivalent.
- [ ] Run focused host tests and package compile/link; commit the record/settings/profile increment.

### Task 5: Prepare and register installations transactionally

**Files:** Create `src/runtime/install_service.{h,cpp}`, `src/runtime/package_installer.{h,cpp}`, `tests/runtime_install_service_test.cpp`, `tests/runtime_package_installer_test.cpp`; consume Task 1's adapter; modify `src/homebrew/controller.{hpp,cpp}`, `src/steam/install_service.{h,cpp}` only as needed to observe download completion, and selected CMake registration.

**Interfaces:** `RuntimeInstallService::Prepare(install_id)`, `Retry(install_id)`, `Snapshot()`; a worker consumes downloaded game records + kit and produces preparation states. The Steam download service retains its existing completion meaning.

- [ ] Add failure-injection tests at template copy, hash verification, profile publication, registration and ready-record write. Restart after each boundary must resume/reconcile without duplicate registration or a false Play action.
- [ ] Stage kit/bottle/title, validate free space and all hashes/modes, publish the descriptor/profile, then register. Confirm registration before committing `ready`; queued scans are pending, and service-cache absence is insufficient for title allocation. Treat stale hooks/unreachable providers as recoverable preparation failures without automatically restarting services. Preserve downloaded game files on preparation failure/cancel.
- [ ] Reconcile already registered owned titles against descriptor/install ID; reject unrelated collisions. Clean only owned incomplete staging files and never overwrite a mutable bottle during retry.
- [ ] Project `Preparing compatibility`, `Registering game`, `Ready` and actionable failure/retry into Details/Downloads. Do not label unsupported games ready.
- [ ] Run host service tests and a console adoption of existing HL2; commit with evidence that no redownload occurred.

### Task 6: Connect the real Play action

**Files:** Create `src/runtime/launch_service.{h,cpp}`, `tests/runtime_launch_service_test.cpp`; modify `src/homebrew/controller.{hpp,cpp}`, `src/homebrew/simulation.{hpp,cpp}`, `src/homebrew/host.cpp`, `src/homebrew/ps5.cpp`; extend selected model/controller tests and CMake targets.

**Interfaces:** `LaunchService::Request(install_id)` and `Snapshot()` perform readiness checks and emit a main-thread handoff request; `TitleBackend::Launch(title_id)` performs the platform call. Host tests inject a fake backend; normal macOS builds report native launch unavailable.

- [ ] Test missing volume/bottle/runtime/helper, duplicate Play requests, changed settings during preparation and failed native launch. Include `0x80940010` followed by startup, the same code without startup, and launcher termination before the result can be persisted. No invalid input may invoke the title backend.
- [ ] Wire live UI Play requests through the controller instead of the simulation session timer. Show `Preparing`/`Launching` and native failure text; never synthesize `Running` from a timer or successful API return.
- [ ] Flush preferences/settings, persist the pending session and bounded launch log, then call the native adapter on its required thread. Remove the one-shot development request path from normal launch behavior.
- [ ] Hardware-test Playden → HL2 and argument-free Home → HL2 using the same profile, then exit/reopen/retry. Commit the real launch increment.

### Task 7: Bind Cloud to the actual bottle and reconcile sessions

**Files:** Modify `src/steam/cloud_service.h`, `src/steam/backend/steam_cloud_runner.{h,cpp}`, `src/homebrew/controller.{hpp,cpp}` and launch service; create `src/runtime/session_reconciler.{h,cpp}`, `tests/runtime_session_reconciler_test.cpp`; persist per-session files from Task 2 rather than relying on a single overwritten session.json; extend `tests/steam_cloud_test.cpp` and `tests/cloud_service_test.cpp`.

**Interfaces:** Add explicit `prefix_root` to `CloudLocation`, retaining a legacy fallback for unprepared installs. `SessionReconciler::Reconcile(records, title_status)` queues an account-aware Cloud check only when the game is no longer active; it does not require Playden to have survived handoff.

- [ ] Test runtime saves map to `library/prefixes/game`, with state/baseline at `cloud/`; migration backs up existing `state_root/prefix` saves and refuses ambiguous differing copies without a choice.
- [ ] Test prelaunch conflict/account confirmation, offline “Play without syncing”, changed account during sync and launch cancellation. Native handoff waits for the chosen resolution.
- [ ] Test clean exit, Home-originated session, crash/missing exit record, duplicate reconciliation and active title. Never upload from an active bottle or assume a missing status means clean completion.
- [ ] Implement per-session next-open reconciliation with persisted completion, backup/conflict behavior and no credentials in wrapper files. Run Cloud/model tests and controlled console save/load/sync acceptance; commit.

### Task 8: Settings, repair, update and uninstall

**Files:** Modify `src/homebrew/simulation.{hpp,cpp}`, `src/homebrew/view.cpp`, runtime settings/install services and package installer; create `tests/runtime_maintenance_test.cpp`; extend graphical management/settings tests.

**Settings spec:** tiers, phases and UI rules are in [game-settings-roadmap.md](../../engineering/game-settings-roadmap.md).

**Interfaces:** `Repair(install_id)` revalidates assets/recipe without replacing saves; `Remove(install_id, remove_saves)` unregisters owned title/files while defaulting to save retention; updates stage a new pinned kit and retain the previous manifest for rollback.

- [ ] Expose preset/reset, launch entry/arguments, validated resolution, controller preset and FPS overlay; put supported graphics/runtime/DLL/log choices in advanced settings. Hide unsupported frame-limit/VSync mappings, DXVK 120 Hz and unvalidated scaling options. The owner-verified HL2 input defaults remain recipe-scoped; validate them again with the new per-game title and fresh bottle.
- [ ] Test settings persistence and identical Home/Playden effective profiles; repair failure retains saves and user overrides. Back up game-root saves before deleting downloaded game files.
- [ ] Verify an existing supported post-exit unregister mechanism; native Playden receives EBUSY while open. If none exists, design and review a narrow helper/worker as a separate dependency before implementing automated removal. Never mark removal complete from request acceptance.
- [ ] Test interrupted uninstall, failed unregister, unavailable volume and running game; resume owned cleanup safely while preserving the allocation until unregister completes. Default uninstall retains bottle/saves/Cloud state; deleting saves is a separate explicit choice.
- [ ] Stage updates without modifying active runtime assets; block update/reset while running. If a recipe migrates the bottle, back it up and include it in rollback; otherwise preserve the bottle. Test failed update restores old runtime/settings and launchability.
- [ ] Run host maintenance/graphical tests and console repair/reinstall/save-retention checks; commit and update status/evidence.

## Remaining-task execution details

This section fixes the interfaces and test boundaries for Tasks 2–8. Names below
are proposed new APIs/test targets, not claims that they exist. Add each target to
its repository's test configuration when writing that task's tests. Use the
[roadmap](../../engineering/prospero-win-roadmap.md) for milestone ordering.

### Shared contracts

- Descriptor v1 is the `game-launch.json` defined above; limit it to 8192 bytes,
  require a nine-byte native title ID, and validate `data_root` against the exact
  `/data/playden/games/<install_id>` root. Reject traversal and symlink escapes.
- Task 2 defines `PwTitleLaunch` and
  `pw_title_launch_parse(bytes, length, expected_title_id, out, error, error_size)`
  returning int zero on success; bytes/expected_title_id/error are char pointers,
  length/error_size are size_t, and out is a PwTitleLaunch pointer. It validates identity before elevation and resolves
  the single configured profile afterwards. No descriptor preserves generic mode;
  a present invalid descriptor fails visibly instead of using generic mode.
- Session v1 stores `session_id`, `install_id`, `title_id`, `origin`,
  `settings_revision`, `runtime_bundle_id`, `phase`, and optional `exit_code`.
  Phases are `starting`, `running`, `exited`, `failed`; `running` requires actual
  game-process startup. Write per-session files under `logs/sessions/<session_id>.json`
  atomically, so a new launch cannot erase an unreconciled prior session.
- Per-game mode accepts optional `session=<id>` for a Playden handoff; without it,
  generate a fresh ID and set origin to Home. Session IDs are UUID strings;
  validate before deriving a session filename. Reject malformed IDs and profile
  selection overrides in this mode. Generic mode retains its existing arguments.
- `RuntimeInstallRepository::Load/Save` owns persisted install/title reservations
  and preparation journals. `BuildEffectiveSettings(recipe, overrides)` returns
  either validated effective settings or a preparation error. Implement its value
  types in `game_install.h` / `game_settings.h`, using the identity fields above.
- In `native_access.h`, define `NativePaths` with string fields `title_root`,
  `trust_store`, `steam_state_root`, `log_root`; define `NativeAccessResult` with
  `bool granted` and `std::string error`. `AcquireNativeAccess(title_id, helper_path)`
  takes two string references and returns `NativeAccessResult`;
  `ResolveNativePaths(title_id)` returns `std::optional<NativePaths>`.
  Resolve readable paths independently of grant success for ordinary browsing;
  preparation and native launch additionally require `granted=true`; no downstream service may cache `/app0` paths across
  a successful grant. Prefer the readable title sandbox app0 path after elevation;
  validate any source-directory fallback against the launcher's owned title.
- `RuntimeInstallService::Prepare/Retry/Snapshot` owns preparation; it consumes
  persisted records and a validated kit. `LaunchService::Request/Snapshot` owns
  launch preflight/pending sessions and emits the native handoff on the application
  thread. It calls the existing `TitleBackend::Launch(title_id, {"session=" + id})`.
- `CloudLocation.prefix_root` explicitly selects the bottle; `state_root` remains
  the Cloud baseline/backups root. `SessionReconciler::Reconcile(records, title_status)`
  consumes durable wrapper sessions and native activity checks. Unknown activity
  blocks upload/maintenance rather than assuming the title stopped.

### Task 2 execution: wrapper and lifecycle

Execution evidence (2026-10-05): the current per-game source worktree remains
uncommitted. Its title-descriptor and title-session tests compile and pass with
GCC and warnings treated as errors in an isolated x86_64 Linux container. The
session test needed `_XOPEN_SOURCE` for glibc `realpath` and checked formatting
for its temporary paths. Full upstream `make test` then stops at the existing
`test_pw_x86_hostexec` low-address `mmap` assertion under Docker emulation; it
has not passed the supported native Linux full-suite gate. The initial ARM macOS
attempt also fails in x86 intrinsics. These limitations do not establish a
wrapper regression or permit marking the full-suite/console gates complete.

- [ ] Step 1: Add `test_pw_title_launch.c` and register it in upstream `Makefile`. Assert
   `HomeUsesDescriptorDefault`, `MissingDescriptorKeepsGenericMode`,
   `InvalidDescriptorNeverFallsBack`, `TitleMismatchRejected`,
   `DataRootEscapeRejected`, `SessionIdentityPreserved` and `GameExitExitsTitle`.
   Extend package tests to reject a helper/manifest for another title.
- [ ] Step 2: Run `make -C ../ps5-homebrew/prospero-win build/host/test_pw_title_launch`
   and its test binary; expect failure before parser/lifecycle implementation.
- [ ] Step 3: Implement the parser and descriptor read-before-grant/bootstrap-after-grant in
   `native/wine64_main.c`; parameterize title roots and package metadata. Add
   explicit local helper/manifest inputs to build/package scripts. Update session
   records only from observed lifecycle events; exit-title suppresses catalog cycles.
- [ ] Step 4: Run the new C test plus `python3 tests/test_title_identity.py` and
   `python3 tests/test_package_release.py` from the upstream checkout. Expected:
   all assertions pass, including existing generic-mode tests. Run upstream
   `make test` in its supported host environment before committing.
- [ ] Step 5: Package an owned FAKE78xxx HL2 title with the real wrapper/runtime and a copied
   private bottle. Verify Home starts its default profile without arguments,
   malformed descriptor fails clearly, and game exit returns to Home. Capture
   title/helper identity, logs and rendered evidence; obtain review and commit.

### Task 3 execution: kit, recipe and submodule

Execution evidence (2026-10-05): the admission library and its existing regression
test are now registered in `cmake/ProsperoRuntime.cmake`, included for both host
and PS5 builds. The normal host suite passes 72/72; the PS5 build compiles the
admission library and packages the launcher successfully. The upstream kit and
bottle preparation tests pass 25/25 and 12/12 locally. These checks do not close
Task 3: source gitlink, clean source provenance, release kit/lock, licenses and
fresh-bottle console acceptance remain required. Registering a validator is not
evidence that an installed kit has been admitted.

Production hash checkpoint (2026-10-05): `VerifyInstalledKit` binds manifest
verification to the real streaming SHA-256 backend, ignoring caller-supplied
hash callbacks. A second run of the manifest cases uses real on-disk SHA-256 and
checks same-size payload substitution, wrong helper pins, unsupported sources,
extra payloads, invalid registry state and symlinked ancestors. All 74 host tests
pass and the PS5 SDK build compiles the adapter. Independent trust pins still
need to be loaded from the final dependency lock and wired into preparation.
Console filesystem support for the full verifier remains unproven: its metadata
and traversal operations still need testing through native access. The adapter
is not yet called by the launcher's production installation service.

Kit manifest contract: `playden-prospero-kit/1`, with separate runtime artifact
revision, template and recipe identity/version, source provenance, sorted per-file
size/hash/mode inventory, and `variants[]` binding each supported title ID to its
wrapper/helper assets. Validate every variant before admitting it to allocation.
This resolves title-specific native compilation while keeping ordinary PS5
installation offline and independent of a host SDK.

- [ ] Step 1: Add `tests/test_playden_kit.py` upstream. Assert rejection of missing syswow64,
   wrong CPU/crypto registry views, missing 32-bit MMDeviceEnumerator/DirectInput8,
   mismatched DXVK, wrong-title helper, traversal, escaping symlinks and missing
   executable modes. Add a valid minimal manifest fixture; run the test before
   implementing the packager and confirm it fails for the missing behavior.
- [ ] Step 2: Implement `tools/package_playden_kit.py` and offline validation by reusing the
   existing prefix/install/runtime tools. Require explicit helper build inputs and
   preserve the tested Wine artifacts; emit per-file hashes/modes and source provenance.
- [ ] Step 3: Add the tested Task 2 source gitlink, `cmake/ProsperoRuntime.cmake`, explicit kit
   build script, recipe and dependency lock. Reject release source overrides at
   other revisions; ordinary Playden builds never invoke Wine compilation.
- [ ] Step 4: Run `python3 tests/test_playden_kit.py` upstream and the selected Playden host
   build/tests from a clean checkout with initialized submodules. Expected:
   reproducible kit validation passes; wrong pin/hash/template fails before copying.
- [ ] Step 5: Prepare a fresh HL2 bottle offline, package it without game files/personal state,
   and test the per-game Home title with the exact recipe. Record hashes, disk/staging
   requirements and licenses; review and commit the source/kit lock together.

### Task 4 execution: records, settings and production access

Hardware checkpoint (2026-10-05): the normal launcher now renders after native
bootstrap, with the cached library/artwork and installed games preserved. The
account name was removed from the top bar at the user's request. Sandbox
`lstat`/`access` failures required operational path checks; the folder-title link
also needed the SDK's `openat` adapter. See [preview evidence](../../../results/20261005-native-startup-preview/README.md).
All 73 host tests pass after the corrections. Native grant still fails with code
13, so this establishes browsing fallback only and does not close Step 5 or
permit connecting preparation/launch as ready. The console was released for the
user to inspect the deployed preview.

Local helper checkpoint (2026-10-05): upstream commit `4e5e2bc` adds
stage/status/error diagnostics and rejects a terminal helper OK when the title's
own credential clone failed. Regression cases cover fragmented I/O, wrong PID,
failed socket options, disconnected transport, failed helper application and
failed clone followed by terminal OK. The focused host test and native Linux
ASan/UBSan run pass. The launcher builds with the detailed API and records it in
startup logs; CMake requires that source capability. This diagnostic build is
not deployed: the user's preview remains available, and the exact console
failure stage is still unverified. Other per-game wrapper changes remain
uncommitted, and `4e5e2bc` is not the final kit/source release pin.

Console follow-up: the diagnostic build renders Home and records a validated
`APPLY_FAILED` reply. Lapy logs show `target_preflight error=16`, with two title
threads where its private credential check requires one. Its root layout probe
passes and no target roots/credentials are changed. See
[helper evidence](../../../results/20261005-helper-diagnostics/README.md).
Startup sequencing must grant access before additional graphics threads, while
preserving graphics module/path availability and browsing fallback. Keep the
helper ownership checks intact. The console was released after both captures.

Sequencing follow-up: display initialization now follows bootstrap; graphics
preparation probes the linked EGL entry point only. Basic network initialization
is separated from the watchdog-based nonblocking preflight, which Steam/curl
still perform after bootstrap. Both experiments preserve rendered Home and
existing state, and the final build passes 74 host tests. However, Lapy still
observes two threads before the grant. EGL initialization and the networking
watchdog are therefore not the sole cause. Isolate UserService, basic network
initialization and earlier module startup next; do not relax credential checks.
See [sequencing evidence](../../../results/20261005-single-thread-bootstrap/README.md).

Deferred UserService follow-up: production now grants access before UserService
initialization, while still completing it before UI/native launch services.
The console helper observes one thread and completes the verified credential
and root transfer. UserService then succeeds and bootstrap reports native
runtime readiness. However, account storage fails in the granted view, so Step 5
remains open. Restoring the prior preview returns Home and the existing library,
confirming saved state remains. The grant-enabled executable is not left on the
console. See [grant evidence](../../../results/20261005-deferred-user-grant/README.md).
All 74 host tests and the PS5 build pass; the PS5 is released.

Saved-session follow-up: the optional repository diagnostic report proves its
private write/read/remove preparation probe succeeds after elevation, but
opening existing `session.v1` returns permission denied. Investigate saved-file
ownership and granted credentials next; do not reset the account or weaken
private-file checks. The known browsing preview was restored and verified.
See [session access evidence](../../../results/20261005-session-access-diagnosis/README.md).

Credential/volume correction: after the helper succeeds, restore the original
effective UID through `seteuid` and verify it. On the console this retains native
managed-data writes and permits reading the existing mode-0600 session in the
stable view, without changing file permissions/ownership. Primary volume
candidates now use the verified title root, preserving the existing installed
game registry after `/app0` disappears. Host regression exercises actual
InstallService scanning through the supplied stable volume; all 74 tests pass.
The deployed build renders Home/library/artwork and both installed entries;
Downloads reports their 5.5 GB of used console storage. See
[native state evidence](../../../results/20261005-native-owner-and-volumes/README.md).
This closes the observed grant/session/installed-inventory startup failures;
it does not establish a fresh network download, settings persistence or per-game
preparation/launch. The successful build is left installed and the PS5 released.





- [ ] Step 1: Add `runtime_game_install`, `prospero_profile` extensions and `native_access`
   tests. Assert `ReservationSurvivesRestart`, `ForeignNativeTitleCollision`,
   `LegacyDownloadAdoptsWithoutCopy`, `RecipeArgumentsPreserved`,
   `ProfileRoundTripsThroughUpstreamParser`, `PathsRemainValidAfterGrant` and
   `GrantFailureDisablesRuntimeOnly`. Verify the new assertions fail first.
- [ ] Step 2: Implement repository/settings value types and atomic persistence, using
   FAKE78000–FAKE78999 reservations checked against native inventory/source paths.
   Keep downloaded/ready meanings separate and preserve the existing HL2 game root.
- [ ] Step 3: Implement bootstrap access and stable path resolution before services/UI are
   constructed; initialize user service before foreground-user/native launch calls.
   Include the launcher's exact-title helper+manifest in its production
   package. Resolve graphics/module loading before or through stable paths too;
   do not assume the headless diagnostic proved live UI operation after elevation.
- [ ] Step 4: Run `ctest --preset homebrew-host -R '^(runtime_game_install|prospero_profile|native_access)$'`
   after building the new targets; expected: all named cases pass. Check generated
   profiles with both upstream profile parser and argument splitter.
- [ ] Step 5: Build/deploy the normal launcher, verify account/library/artwork/download state
   survives the grant and reopen, and verify saved settings/profile identity.
   Review and commit before connecting preparation to the UI.

### Task 5 execution: preparation and readiness

- [ ] Step 1: Add `runtime_install_service` and `runtime_package_installer` tests. Inject
   failure/cancel at copy/hash/profile/registration/ready-write boundaries; assert
   preserved downloads/saves, no duplicate ID, recoverable journal and no false Play.
   Include `ScanQueuedIsPending`, `StaleHookRetainsJournal` and `CacheAbsentIsNotAllocation`.
   Run the new cases and confirm failures before implementing the transitions.
- [ ] Step 2: Implement staged publication, bounded copying/space checks, modes, ownership
   checks and registration through the existing Task 1 registry transport. Never
   overwrite a mutable bottle during retry; reconcile native completion before Ready.
- [ ] Step 3: Wire download completion into preparation eligibility and project preparation,
   registration, failure and retry states into Details/Downloads; unsupported recipes
   remain downloaded. Do not automatically restart console payloads for stale hooks.
- [ ] Step 4: Run `ctest --preset homebrew-host -R '^runtime_(install_service|package_installer)$'`
   after building; expected: failure-injection/restart cases pass at every boundary.
- [ ] Step 5: Adopt the current HL2 download without network redownload, prepare/register an
   owned title, interrupt and retry once, and inspect Ready/profile/ownership records.
   Archive evidence, review and commit.

### Task 6 execution: real Play and startup reconciliation

- [ ] Step 1: Add `runtime_launch_service` tests for missing inputs/media, duplicate requests,
   settings changes, state flush, native errors, launcher disappearance and pending
   sessions. Assert `Error80940010ThenStartupIsReconciled`,
   `Error80940010WithoutStartupRemainsFailedOrUnknown`, and `TimerNeverMeansRunning`.
   Run the cases and confirm they fail before wiring native launch.
- [ ] Step 2: Implement readiness/volume preflight, durable pending session and bounded launch
   diagnostics. Invoke the existing native backend on the application thread after
   state flush, with the optional session argument described above. On reopen,
   reconcile observed wrapper status; never infer final failure solely from 80940010.
- [ ] Step 3: Connect live UI Play to the service, retaining simulation only for explicit
   fixtures. Remove one-shot developer launch requests from production behavior.
- [ ] Step 4: Run `ctest --preset homebrew-host -R '^(runtime_launch_service|homebrew_live_model|homebrew_simulation)$'`
   after building; expected: invalid inputs never invoke the native backend and
   existing host/live UI behavior remains covered.
- [ ] Step 5: Verify Playden → HL2, exit/reopen/retry, and argument-free Home → HL2 with identical
   effective settings. Correlate session IDs, native results, service handoff and
   target startup. Until Task 7 maps Cloud to the real bottle, use explicit local-save
   play and do not invoke the legacy prefix mapping for prepared installs.
   Review and commit; this closes M2, not the Cloud milestone.

### Task 7 execution: actual-bottle Cloud and sessions

- [ ] Step 1: Extend `cloud_service`/`steam_cloud` tests and add `runtime_session_reconciler`.
   Assert actual prefix_root mapping, backed-up migration, ambiguous save conflict,
   account switch, explicit offline play, clean/crashed/Home session, duplicate
   reconciliation and no uploads from active or unknown-status titles. Confirm failure.
- [ ] Step 2: Add explicit bottle mapping to CloudLocation/runner and integrate prelaunch Cloud
   decisions with LaunchService. Persist reconciliation per session/account; migrate
   legacy saves only with backups and explicit resolution of differing copies.
- [ ] Step 3: Run `ctest --preset homebrew-host -R '^(cloud_service|steam_cloud|runtime_session_reconciler|runtime_launch_service)$'`
   after building; expected: conflicts wait for a choice, cancelled sync prevents
   handoff, and repeated next-open reconciliation is idempotent.
- [ ] Step 4: On the console, save/close/reload through both launch paths, then test offline play
   and a controlled Cloud conflict with backup files. Inspect baseline/account state
   and wrapper session records; do not infer save mapping from successful menu launch.
- [ ] Step 5: Review and commit with console evidence, closing M3.

### Task 8 execution: settings and maintenance

- [ ] Step 1: Establish whether the existing service supports post-exit unregister. If not,
   write a separate helper/worker design with ownership checks, bounded lifetime,
   persisted results and no credentials; complete its review before adding the
   dependency. A UI-only retry of the current EBUSY call cannot close this gate.
- [ ] Step 2: Add `runtime_maintenance` tests for running/unknown title, interrupted removal,
   failed unregister, source retirement recovery, missing volume, save retention,
   settings reset, failed update and rollback of runtime/settings/bottle migration.
   Confirm they fail before implementing maintenance transitions.
- [ ] Step 3: Implement validated settings, owned repair, staged kit update/rollback and durable
   removal through the reviewed execution path. Keep reservations until native
   disappearance is confirmed; default remove_saves=false and preserve Cloud backups.
- [ ] Step 4: Run `ctest --preset homebrew-host -R '^(runtime_maintenance|homebrew_management_tour|homebrew_live_model)$'`
   after building; extend the registered management/live-model tests with settings
   and maintenance UI cases. Expected: maintenance failures
   preserve launchable prior assets or a recoverable owned removal journal.
- [ ] Step 5: Verify console repair/reinstall/save retention, interrupted uninstall recovery and
   failed-update rollback; then execute final release acceptance below. Review and
   commit with exact kit/helper revisions and evidence, closing M4.

## Verification and release gates

All newly named test targets must be registered in CMake/Makefile before verification. A focused run with zero matched tests is a failure, not acceptance.

For each task, run newly added focused tests first, then the selected host build/suite when the increment is complete. Register tests in the selected CMake targets. Run legacy checks only for changes to shared code; run upstream C/Python contract suites for wrapper/kit changes. A failing hardware gate leaves the corresponding milestone open.

```sh
cmake --build --preset homebrew-host -j 4
ctest --preset homebrew-host --output-on-failure
scripts/build-homebrew-ps5.sh
git diff --check
```

Final console acceptance: fresh validated bottle; launch from Playden and Home; start a level; movement/look/actions/menu controls; audio; save, close and load after restart; Cloud reconciliation and conflicts; unavailable volume recovery; repair/update rollback/uninstall save retention. Record HDMI evidence and startup/helper/Wine logs with exact artifact versions. Run at least 20 minutes of gameplay and report measured pacing/crashes separately from the existing menu FPS overlay; no blanket 60 fps claim.

## Preserved design decisions

1. Use the approved per-game title/bottle and bundled-runtime model, with shared runtime deduplication deferred.
   Use a pinned `third_party/prospero-win` source submodule and separately versioned, hash-verified binary/template kits.
2. Use prebuilt validated compatibility kits and curated recipes as the first preparation mechanism; generic host-dependent installers remain unsupported.
3. Use exit-to-Home plus next-open Cloud reconciliation; Home launches initially use local saves.
4. Keep HL2-only initial acceptance and save-preserving uninstall by default.
5. Use Task 1’s measured platform contract; complete production access and collision checks before automatic installation. Any required persistent broker gets its own reviewed design.

Recommended order: prove platform contracts → upstream wrapper + reproducible kit → adopt HL2 and enable real launch → Cloud/lifecycle → maintenance and broader recipes. Tasks 1–6 establish the first launch milestone; Tasks 7–8 complete managed runtime integration.

### 2026-10-06 execution follow-up: private data and settings handoff

The settings handoff through `47db605` is merged (`473cb1b`), with unsupported/malformed community requirements corrected (`1f75230`). Combined host tests remain 76/76 and the full native SDK package builds. New input/audio runtime capabilities remain disabled until their separately patched runtime is built and verified.

App-owned private bottle copying, full private ancestor admission, pre-write file ownership/modes, and initial-marker retry recovery are committed (`9d023ce`). Private profile/input publication readiness is also verified. The production descriptor-relative API still fails on PS5; libc and kernel-created directory FDs both produce relative `openat` errno 22. Do not connect this preparation path as ready until the native API contract is resolved.

The wrapper's private session repair passed its host regression and native build. PS5 acceptance confirmed app UID1/mode0600 session persistence and private log directories for an intentionally failed startup, with ordinary credentials restored after the helper. The original public fixture logs were preserved. Full wrapper success/exit and runtime/gameplay acceptance remain outstanding. See [private-session evidence](../../../results/20261006-private-session/README.md) and [implementation details](../../engineering/results/2026-10-06-private-bottle-copy.md). The normal preview was restored and the PS5 released after testing.

The isolated baseline-plus-0895 Wine build now exits 0 with 11 PRX and 14 selected
PE DLLs; [exact inventory and limitations](../../../results/20261006-wine-build/README.md)
are recorded. This does not replace the retained runtime or close kit acceptance.
The wrapper development branch is `3d05a57`, with native foundation `9c0b994`
unchanged and PRX tooling isolated at `182f1ea`. Native directory aliases also
failed (`ENOENT`), leaving both production private copying and descriptor-relative
settings persistence gated. The launcher package rebuilt with the developer probe
disabled; it has not been deployed for new UI acceptance.

Task 3 source/tooling follow-up: `third_party/prospero-win` now pins the clean
development source `3d05a57`, with an upstream HTTPS URL and `published: false`
in the dependency lock. CMake validates the source revision/cleanliness before
compilation; the explicit offline kit script applies the same check. Release
source validation refuses the unpublished pin. Five upstream portable parser/
session contracts are registered in Playden's host suite, which passed 82/82;
the PS5 launcher package also built from the submodule with diagnostics disabled.
Wrong-revision CMake overrides are rejected. See [kit instructions and limits](../../../runtime/kits/README.md).
Remote clean-checkout distribution, immutable kit/manifest hash, producer-to-
consumer validation and fresh-bottle acceptance remain open; no Task 3 completion
or replacement-runtime capability is claimed.

Task 3 producer/consumer regression: the pinned Python packager now emits a
synthetic kit that the C++ production SHA-256 admission backend consumes. The
fixture uses the Enhanced-provider selection independently in both Wine registry
views; it retains the repaired 32-bit MMDeviceEnumerator/DirectInput8 shape.
Changed payloads, extra files, a wrong manifest pin and a self-consistent helper
replacement are refused. Synthetic fixture trust overrides are test-only.
This closes the format-boundary test gap, while admission of the actual release
runtime/template and its independently bound preparation provenance remains open.

Task 3 preparation-binding follow-up: Playden's explicit packaging entry point
now requires reviewed initialization evidence from the independent dependency
lock. It verifies all four metadata pins and their source/loader/prefix digest
chain, all 1833 entries of the retained v16 initialized export, and the exact
approved console template inventory including modes and directories before
copying. The reviewed console derivation is limited to DXVK, the retained CPU
DLL, drive-table conversion and CPU/audio registry bindings. Tests reject prefix,
template, permission, directory and metadata drift plus unprepared specs.
This establishes the host preparation identity gate, not release-kit/runtime
provenance, remote distribution, native directory operations or gameplay.

Task 7 mapping increment: `CloudLocation` accepts an explicit `prefix_root` for
the actual managed bottle while retaining the legacy state-folder prefix for
unprepared installs. `SteamCloudRunner` binds each pending plan to the exact
game/prefix/state location checked; a changed location invalidates that plan
without transferring files and requires a new check. Host fixtures upload from
`library/prefixes/game` with baseline/state at `cloud/`, and verify the legacy
mapping, service request propagation and refusal after a bottle switch. The
location guard's regression failed when the guard was temporarily removed.
Controller runtime-record wiring, backed-up legacy save migration/conflict
resolution, persisted next-open session reconciliation and console acceptance
remain open. Do not enable managed sync before those production gates are met.

Task 7 storage increment: production `FilesystemReconcileSessionSource` and
`DurableReconcileJournal` now supply bounded private filesystem enumeration and
durable per-session journals. The source binds canonical game roots and the
complete bottle owner marker, preserves historical runtime/revision records for
individual reconciliation, and refuses ambiguous temporary/lock or excess evidence.
The parser matches the pinned C writer's UUID grammar; an actual-writer regression
caught and fixed a narrower UUID check. Host and explicit PS5-policy tests are
registered in CMake. Full host suite passed 92/92; the production session storage
archive compiled with the PS5 SDK. Independent review also identified impossible
Cloud completion records without operation IDs; load/save now reject them, and
focused regression tests passed 3/3 after the fix. Review found no remaining
actionable defects. This does not complete Task 7: native session
acceptance, Cloud/account/controller callbacks, backed-up save migration and evidence
archival/recovery remain open. See [filesystem contract](../../engineering/native-managed-filesystem.md).

The normal preview following private settings setup was deployed and verified by
file readback and native Home startup; see
[deployment evidence](../../../results/20261006-managed-filesystem/README.md).
Console ownership was released. The separately reported Linux RADV runtime build
is [development evidence](../../../results/20261006-linux-wine-build/README.md),
with a title-unavailable `mkstemp` import and remaining admission/native gates;
it does not replace the retained tested runtime or change capability claims.
