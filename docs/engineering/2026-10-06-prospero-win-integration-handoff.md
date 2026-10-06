# Prospero-win integration handoff — 2026-10-06

**Goal:** Complete Playden’s production integration of the pinned prospero-win runtime so each supported Steam game has a private bottle and its own PS5 title, with recoverable preparation/settings publication, safe launch and Cloud/save lifecycle, and verified Half-Life 2 gameplay.

This handoff captures an incomplete implementation, including intentionally unfinished changes in two checkouts. Continue from those changes; do not reset them or treat the last passing committed tests as proof that the current working tree passes.

## Start here

1. Read the [approved integration plan](../superpowers/plans/2026-10-05-prospero-win-integration.md) and [managed-publication continuation](../superpowers/plans/2026-10-06-managed-publication.md).
2. Read [bottle/Wine settings](wine-bottle-settings.md), [title contract](prospero-title-contract.md), [native managed filesystem](native-managed-filesystem.md), and [roadmap](prospero-win-roadmap.md).
3. Read the local, ignored progress ledger: `.superpowers/sdd/2026-10-05-prospero-win-integration/progress.md`. Preserve it. It contains decisions and evidence beyond the committed documents.
4. Inspect both working trees below, then finish the explicit recorded game-root contract before proceeding to production composition.

The user authorized unattended implementation, local commits, builds, local upstream fixes, and PS5 deployment/testing. **Do not push.** This overrides the original plan’s suggestion to publish upstream commits before pinning: local committed development pins are permitted, with `published: false`. Do not claim clean-checkout release availability.

Release the PS5 whenever it is not actively needed. Icon work is deferred until the end. Preserve downloaded games, saves, original game DLLs, and unrelated untracked files. When asking a peer agent to do work, request ACK and do not depend on it before acknowledgment; peers may not wake up. Working alone is permitted.

## Exact checkout state at handoff

| Checkout | Branch | HEAD before this documentation commit |
| --- | --- | --- |
| `/Users/leolobato/Documents/Projetos/Personal/GameNative/playden-ps5-launcher` | `feat/prospero-win-integration` | `b26a424` — recover requested settings updates on ready titles |
| `.worktrees/prospero-win-per-game` beneath that repo | `feat/playden-per-game-title` | `79cf85c065e49ae57679ff5babb88bd290b9e9e9` |
| `third_party/prospero-win` | pinned submodule | `79cf85c065e49ae57679ff5babb88bd290b9e9e9`, clean |

The sibling investigation repo supplied by the user is `/Users/leolobato/Documents/Projetos/Personal/GameNative/ps5-homebrew/prospero-win`. Current integration changes are in the isolated upstream worktree above. Do not confuse it with the clean pinned submodule used for builds.

Parent coding WIP, deliberately uncommitted:

```text
src/runtime/settings_publication_journal.cpp
tests/runtime_settings_publication_journal_test.cpp
tests/runtime_settings_publication_test.cpp
```

Parent also has untracked `tests/__pycache__/` and `tools/__pycache__/`; leave them alone.

Upstream coding WIP, deliberately uncommitted:

```text
native/pw_title_publication_native.c
src/pw_title_launch.c
src/pw_title_launch.h
tests/test_pw_title_launch.c
tests/test_pw_title_publication_native.c
```

Upstream untracked `probes/` predates this increment; preserve it. No upstream WIP has been committed, fetched into the submodule, or reflected in the dependency lock yet.

Earlier `feat/game-settings` and `feat/icon` work was already integrated; do not blindly merge it again. Their historical worktrees are under `/Users/leolobato/.chauffeur/worktrees/18C0FEEF-6A3F-52E4-64F1-24A93D005389/`, with branch heads `f92e94a` and `51f9ed6` respectively. Review actual ancestry if further changes arrive.

No known build/test process or pending execution handle remained at this handoff. Previous execution sessions completed and were polled. The PS5 was restored and released.

## Immediate unfinished problem: game files are independent of the native title

The original design keeps game files at `<selected volume games_root>/<app_id>`. For example, adopt HL2 at `/data/homebrew/FAKE77031/games/220`, while the newly allocated native title is a different `FAKE78xxx`. Its wrapper/runtime/helper/descriptor live under `/data/homebrew/<title_id>`, and its private bottle lives under `/data/playden/games/<install_id>`.

The frozen-operation validator and upstream native publication verifier incorrectly inferred the game root as `/data/homebrew/<new title_id>/games/<app_id>`. This prevents adoption of existing downloads and USB volume routing. **Do not move or redownload games to work around this bug.**

### Parent WIP and current failing regression

`BaseValid` in `settings_publication_journal.cpp` no longer requires the inferred per-title download path. Other runtime-record validity, profile executable/working-path, metadata, digest, and receipt checks remain.

The journal regression freezes, encodes, and decodes operations for both `/data/homebrew/FAKE77031/games/220` and `/mnt/usb0/playden/games/220`, while still rejecting a mismatched recorded root. That regression passed after this change.

The real filesystem publisher regression uses both roots, checks publication of `dxvk.conf` at the actual recorded location, and preserves `probe.exe`, `steam_api.dll`, and `player-save.dat` byte-for-byte. It also checks that no wrong per-title DXVK path or package staging mutation occurs and that initial registration occurs once.

That publisher regression **still fails** against the old pinned shared native verifier:

```text
Adopted publication: publication-verification publication admission refused at stage 6
published.state == PackageActionState::kComplete && native.registrations == 1
```

Logs are local build artifacts:

- `build/publication-download-root-red-build.log` and `...-red-test.log`: initial regressions.
- `build/publication-download-root-green-build.log`: parent compilation after the narrow change.
- `build/publication-download-root-green-test.log`: despite the filename, its latest run is the failing publisher test above.

No complete passing suite is claimed for the current parent WIP.

### Upstream WIP: explicit schema-2 `game_root`

`PwTitleLaunch` now appends `char game_root[1024]`. The strict descriptor parser accepts schema 1 or 2:

- Schema 1 requires exactly the original 11 fields and infers the historical per-title game root. It rejects an extra `game_root` field.
- Schema 2 requires all 12 fields, including explicit `game_root`.
- Explicit roots must be canonical console paths under `/data/` or `/mnt/`; empty components, dot/dotdot, trailing slash, backslash, and malformed/control string bytes are refused. Unknown versions are refused.
- Parser output is assigned only on complete success.

Native admission now derives executable/cwd containment from `reread.launch.game_root`, parsed from actual descriptor bytes reread from disk and matched to the observed bytes. It does not trust the caller’s mutable pre-parsed structure. Existing directory ownership/inode checks, path boundaries, hashes, receipts, registry semantics, and profile-derived DXVK binding remain.

Tests cover existing-download and USB roots, missing schema-2 root, schema-1 extra root, unknown version, invalid roots, rehashed profiles outside the explicit root, and caller-structure tampering. The latter changes `before.launch.game_root` while retaining valid disk bytes and verifies that admission uses the disk contract.

Four focused upstream binaries passed in the latest recorded run:

```sh
make -C .worktrees/prospero-win-per-game \
  build/host/test_pw_title_launch \
  build/host/test_pw_title_publication \
  build/host/test_pw_title_publication_cpp \
  build/host/test_pw_title_publication_native -j4
```

Run each resulting binary explicitly. Evidence: `build/download-root-upstream-focused-build.log`, `...-parser-test.log`, `...-publication-test.log`, `...-cpp-test.log`, and `...-native-test.log`. `build/download-root-upstream-red-build.log` captures the initial missing API regression.

An attempted full upstream `make test` failed on unrelated macOS ARM build limitations: strict-C11 `mkdtemp` declarations and x86/SSE host execution tests. See `build/download-root-upstream-test.log`. Do not describe the full upstream suite as passing or expand scope to speculative fixes. Use an appropriate Linux/x86 environment if full upstream verification is required.

### Next actions, in order

1. Review the five upstream WIP files, including exact schema masks, root bounds, malformed/duplicate fields, legacy behavior, and byte-bound containment. Extend meaningful regressions if needed.
2. Run focused upstream tests and `git diff --check`; commit only the intended five files locally. Preserve `probes/`; do not push.
3. Fetch that local commit into `third_party/prospero-win` and check out the exact revision. Keep the build source clean.
4. Update `dependencies.lock.json` to the new revision with `published: false`. Keep production publication-guard trust disabled.
5. Update pinned `REVISION` values in `tools/build_publication_acceptance.py` and `tools/run_publication_acceptance.py`. Rebuild affected artifacts; never relabel old binary evidence as a new source revision.
6. Update `CaptureSettingsPublicationOperation` in `src/runtime/settings_publication.cpp` to emit descriptor schema 2 with `game_root = record.install_path`. Update `tests/settings_publication_fixture.h::Refresh` and other emitters found through `rg`; retain meaningful schema-1 legacy coverage.
7. Have `BaseValid` compare the parsed descriptor’s `game_root` to the recorded install path. Schema 1 should remain limited to its historical inferred root; schema 2 supports the recorded independent root.
8. Re-run the parent codec and actual filesystem publisher preservation regressions, then the full parent host suite.
9. Build the actual SDK package and current SDK v0.42 Wine wrapper from clean pinned sources. Record source, artifact hashes, and exact qualification scope.
10. Commit the parent increment and continue to actual production composition. Existing-path adoption still needs real PS5 qualification.

`tools/prospero_source.py` enforces exact revision and a clean source tree; release builds additionally require published provenance. Use the clean submodule rather than packaging the dirty development worktree.

## Committed implementation available to continue from

| Commit | Result and limits |
| --- | --- |
| `b26a424` | Ready-title requested settings recovery; launch gate for any OWNER journal; cold recovery before capture and after prepared intent. Last recorded full parent suite: 108/108, 25.96 seconds. SDK v0.43 archives and actual developer package link passed. No hardware Ready-update acceptance. |
| `abc14c3` | Durable immutable requested-plan handoff, PWRQ1 codec/store, initial cold recovery, and native registration submission fix. Full suite 108/108. PS5 request-store diagnostic passed; it did not publish/register a title. |
| `ebe3ac3` | `Ps5NativeCoordinator` composes source, native database, ShadowMount cache, OWNER, and inspected package state. Full suite 108/108, actual SDK link, read-only native source/cache diagnostic. No full owned-title registration acceptance. |
| `8a37375` | Independent read-only native SQLite title inventory with bounded, fail-closed queries. Native database diagnostic passed. A database row alone does not confer ownership or Ready status. |
| `e01c9d6` | Native title activity query qualified on FW13.60; stable repeated complete inventories required for stopped, uncertainty means Unknown. This is a snapshot, not a future-launch lock. |
| `3aafb1a`, `87467e3`, `35805d9` | Filesystem installer publication admission, install-service routing/persisted OWNER UUID recovery, and private durable control initialization. |
| `e628f9b`, `451569c`, `a9fe312` | Durable launch service, production session storage, and private settings storage/migration. Components remain to be wired into the production flow. |
| `a7d70d9` | Preview evidence and development Wine VM inventory; no new runtime promoted. |

Evidence directories include `results/20261006-ready-settings-request`, `results/20261006-publication-request`, `results/20261006-native-coordinator`, `results/20261006-title-database`, `results/20261006-title-activity`, and `results/20261006-console-services`. Read each README/verification record to distinguish host coverage, SDK link, synthetic native diagnostic, and actual product acceptance.

### OWNER and immutable requests

`SettingsPublicationRequest` uses bounded PWRQ1 encoding (schema 1, 2 MiB), transaction UUID, kit identity, complete runtime record, immutable launch/settings plan, registry timestamp, and canonical SHA digest. Its private `settings-request-<UUID>.v1` is owned `0600`, single-link, exclusively and durably written, then independently read back. A pre-arm orphan grants no automatic capture/adoption authority.

`SettingsPublicationOwner::Request` persists the immutable request before arming OWNER. `ResumeRequested` uses saved initial/Ready-update intent and the frozen prepared operation, not newly recomputed settings. InstallService recognizes the corresponding journal arms and routes publication.

For a Ready update, Request preserves Ready and the old committed revision while arming `settings-request/capture-update`. Desired revision must be newer, package inspection complete, activity positively stopped, and ownership coherent. Launch Request and Handoff refuse any OWNER journal. Publication enters Preparing before live effects and advances revision only on commit. Recovery must not re-register the existing title or rebuild its bottle.

Native Register must receive the currently persisted armed record (`*record`), not the old frozen `operation.install`. The old argument failed the strict state/revision/journal identity seam; `abc14c3` fixes it.

Explicit orphan reconciliation and recovery UI remain open. Refusal of an inert orphan is not completion of all recovery requirements.

### Native ownership and availability

`src/platform/ps5/native_coordinator.{h,cpp}` is worker-synchronous. Allocation checks the managed FAKE78 range against independent source, database, and cache evidence. Public existing sources are occupied and cannot be adopted merely because they contain files. Unknown/inaccessible/symlinked roots are Unknown; cached entries occupy slots. A scan-pending database registration must not cause automatic resubmission.

Owned package admission requires the exact mapped source path, private markers, valid manifest/artifacts, matching bounded `param.json`, and coherent native database identity (`title`, `content`, `/user/app/TITLE/sce_sys`). Registration requires the current persisted Registering arm and stopped activity. It never self-rearms an uncertain request.

SQLite 3.53.1 was vendored from the pinned CheatRunner source with per-file hashes. The inventory opens `/system_data/priv/mms/app.db` read-only, bounds sizes/query work, rejects symlinks, and returns Unknown for incomplete queries. Do not interpret unsupported native status fields speculatively or modify this database.

Native activity uses the qualified FW13.60 process stride (1096 versus SDK structure size 1088) and `sceKernelGetAppInfo` through libkernel handle `0x2001`. A successful system identity with app ID zero is valid if its title is valid. Positively stopped requires two complete identity passes within three identical inventories; failure/race is Unknown.

## Production composition still missing

The selected production entry is **`src/homebrew/ps5.cpp`**, with `src/homebrew/controller.{hpp,cpp}`. The legacy RmlUI `src/app/application.*` and `src/platform/ps5/main.cpp` are not the selected integration targets.

Current production startup has Steam auth/library/install/Cloud services and settings setup, but runtime preparation/OWNER/launch lifecycle remains uncomposed. Simulation or download-complete state must not enable Play.

Compose the existing real components on the worker:

- `VerifyInstalledKit` using independently trusted manifest/source/helper pins and production hashing.
- `FilesystemPackageInstaller` for private directories, verified template copying, exact artifact inspection and publication callbacks.
- `RuntimeInstallRepository` with independent native availability for durable title reservations.
- `RepositoryInstallRecordStore`, `Ps5NativeCoordinator`, `SettingsPublicationOwner`, and `RuntimeInstallService`.
- Launch/session storage, actual profile/descriptor reader and native handoff, Cloud callbacks, and controller status/settings actions.

Resolve object lifetimes explicitly: repository availability consults the native coordinator; installer publication calls OWNER; the worker owns durable work; the main thread performs native handoff. Keep Steam download records separate from runtime readiness. Preserve existing volume routing and game files.

Playden creates private owned directories itself, then copies uploaded template contents into them. Do not keep debugging FTP ownership as the provisioning solution.

## Build and verification

Parent host build and full suite:

```sh
cmake --build build/homebrew-host -j4
ctest --test-dir build/homebrew-host --output-on-failure
```

Last passing committed evidence: `build/publication-update-host-build.log` and `build/publication-update-host-test.log`, 108/108. These results predate the current failing WIP. A separate `build/host` core focused suite passed earlier; it is not a substitute for the selected homebrew suite.

Actual SDK v0.43 developer package build:

```sh
bash ../playden-ps5/scripts/with-env.sh cmake --build \
  build/homebrew-ps5-publication-native \
  --target playden_runtime_settings_publication playden_runtime_launch_service \
           playden-homebrew-ps5-package -j4
```

Last committed build log: `build/publication-update-sdk-build.log`. Raw linker assembly in `cmake/HomebrewPS5.cmake` requires explicit archive inputs and DEPENDS; CMake PUBLIC dependencies alone do not propagate into the manual `prospero-lld` invocation. Compile-only success is insufficient. A previous final-link failure omitted `playden_game_paths`; that dependency was added.

`PLAYDEN_TITLE_CONTRACT_PROBE=ON` creates a developer diagnostic package, not the finished normal UI pipeline. The normal preview is built with diagnostics OFF. Wine wrappers use foundation SDK v0.42, while Playden native diagnostics currently use SDK v0.43; qualify both actual boundaries.

## Pins and trust boundaries

| Component | Required source/hash |
| --- | --- |
| Current clean prospero-win pin | `79cf85c065e49ae57679ff5babb88bd290b9e9e9`; will change after committing explicit-root WIP |
| Historical required native startup patch | `824b4ec960b9ee93365437d7bb063db1d0e44b28`, included in original `54caac176e86c60c7c407972583b491f67838ed9` baseline |
| Retained tested Wine runtime | `e16ec8f3a19dec4a1162e7554ab73ce41f83bdb9` |
| Patched Lapy base | `611fa5f7ed46cdde7094b764168390ab5c4e89f8` |
| Exact FAKE-title helper extension | `e9f2cfc2c3913e42fdf83d74291e270b041ff0d9`; do not label an extended build as bare `611fa5f` |
| Foundation / wrapper SDK | `9c0b994a048521af6fb84c73ded364504fe250e9`, SDK v0.42 |
| Foundation SDK archive SHA-256 | `8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da` |
| FAKE78001 reviewed helper SHA-256 | `5608915201204dfcd1846b01dd2a9e60d03aa390c69b1dc61f3b150566bf1935` |
| Publication provider provenance SHA-256 | `f14d5e3ecb31951e3cf5bfdc41972181a9be1d96ed0dc7cb3aeebb8e4aab844a` for `third_party/mbedtls-publication/provenance.json` |
| DXVK 2.6.2 archive SHA-256 | `17761876556afd55736cb895d184f5a1c55d43350f1b1e3b129f8d28706d7992` |

Production publication-guard capability/trust remains **disabled** until actual native qualification. A candidate’s self-declared source IDs/hashes are not independent trust anchors. Preserve exact SQLite/provider provenance and per-title helper qualification.

The clean 79cf85c SDK v0.42 FAKE78001 wrapper in `third_party/prospero-win/dist/FAKE78001-publication-publisher` has eboot SHA `07be74885397808e51179c6810af2501ba88ec34757ee6a34e9dce24bad660b7` and ELF SHA `285beafbf18dfb1af0036c407b2fb3fef3051a23df5ae4ba6c90ff0ee58c1bfb`. It is not native-qualified and must be rebuilt for a new source pin.

Offline bottle preparation must repair the 32-bit MMDeviceEnumerator CLSID `{BCDE0395-E52F-467C-8E3D-C4579291692E}` to `mmdevapi.dll` and DirectInput8 `{25E609E4-B259-11CF-BFC7-444553540000}` to `dinput8.dll`, including corresponding InprocServer32 `ThreadingModel="Both"`. These audio/controller repairs are bottle registry changes, not extra Wine code patches. Preserve original `steam_api.dll`.

Known release/qualification blockers:

- The retained runtime’s PE `opengl32.dll` contains a personal host path. Packaging must refuse unsanitized release artifacts; resolve provenance/privacy before promotion.
- New development Wine is blocked by `libvulkan`/`webkit_unboundmkstemp`. Keep the retained runtime; do not substitute an unqualified new build.
- Standalone SDK v0.42 provider resolution through fixed handle `0x2001` failed ENOSYS. The SDK v0.43 Playden bridge working does not qualify the SDK v0.42 Wine wrapper. Do not invent a fallback outside the reviewed contract.
- Retained runtime child-process creation returned `0x550 STATUS_NOT_SUPPORTED`; no child-lease propagation claim is established.
- A shared lifecycle reader lease must cover the actual entire `wine64_main`/title lifetime. Host locks and synthetic native storage diagnostics do not prove this.

## PS5 state and safe testing procedure

**At handoff the console is released, the diagnostic is stopped, and the normal preview is restored.** Do not claim the console merely to read this document. User explicitly requested releasing it whenever unused.

Last independently verified restoration, recorded in `build/title-activity-restoration.json`:

| File | SHA-256 | Mode |
| --- | --- | --- |
| `eboot.bin` | `d76d1a42306daf024d792da5497c29a8152a8341d71470e86f79ab8bd4308daa` | `0755` |
| `playden-package.identity` | `c7593741974bfd517df3335f9ecfe6b41a17fafd4f3c0f0432ca548a6921fb40` | `0644` |

Backups are `build/title-activity-native-backup/{eboot.bin,playden-package.identity,manifest.json}`. Stopped diagnostic PID was **1168**; never kill a reused PID based on this stale record.

Console endpoint was `10.0.1.68`, firmware `0x13600007` / 13.60:

- FTP: anonymous zftpd on 2120.
- PLDM: 8084, `/processes_list` and `/process_kill?pid=...`.
- Raw ELF: 9021; MCP/controller 9305 is a different endpoint.
- WFM: 8888.
- ShadowMountPlus API: console loopback `127.0.0.1:10101`; refusal from LAN is expected.

Last checked kstuff PID92 and ShadowMountPlus PID1095 were running, scans were healthy, and diagnostics mounted successfully. These are historical observations; recheck live processes before testing. No preventive restart was needed. A prior retry-limit/mounting episode recovered by restarting the same installed SMP without rebooting. The SMP “already disabled” log is misleading for this kstuff build; it is not a reliable kstuff state predicate.

Local helper `build/title_activity_console.py` currently drives the **`publication-request\n`** diagnostic. Its names/log paths retain historical title-activity/source naming. Review it before reuse:

1. Claim PS5 and get fresh process inventory; do not overwrite a user’s running app.
2. Verify the normal preview hashes and backups before replacing files.
3. Stage upload with `.activity-stage`, set `0755`/`0644`, verify bytes, then rename. Direct overwrite via FTP previously returned 550; executable without `0755` failed CE-107750-0 / `0x80aa001a`.
4. Launch FAKE77031 through MCP. Poll the log until the fresh `begin pid=` segment reaches `end`; the first read can show the previous run.
5. Stop only the exact verified diagnostic process, restore normal files through staged verified rename, independently read back hashes/modes, then release PS5.

Latest request diagnostic package eboot SHA was `93ba6a26b318927746ac532f19a46a48dc3265432640cd65ffba2fa11a58c827`, identity SHA `338cebc77ab7c4993f49dcbe7aca21cc30e7c6d71e83a9236608a6aac732ad91`. PID1168 proved private request storage roundtrip/exclusive overwrite refusal in `/data/playden/request-probe-1168` with EUID1 and 770-byte encoded fixture. **It did not publish or register a native game title.**

MCP tools can be discovered through `ALL_TOOLS`; use claim/release APIs consistently. Avoid dumping image base64 into text output. Do not rely on old diagnostic IDs or execution handles.

## Remaining work and completion criteria

The following requirements remain in the original scope; component test success is not completion of them:

1. Finish the explicit recorded game-root producer/consumer contract, clean pin, SDK builds, and native existing-download/USB adoption qualification.
2. Complete real immutable kit/template admission, offline repaired bottle provenance, per-title patched helper replacement, and packaging release gates.
3. Compose real runtime services in selected production startup/worker/controller, including startup recovery and adoption of existing downloads without movement.
4. Implement explicit orphan/corrupt/unavailable recovery and uncertain-registration UI; never automatically resubmit uncertain native registration.
5. Wire settings prepare/apply/status for initial and Ready updates. Explain unsupported settings with concrete missing implementation reasons from the settings catalogue.
6. Wire real Play/native handoff, Cloud resolution/offline choices, durable pending/history/exit reconciliation, and main-thread launch constraints.
7. Complete game-scoped saves and account-aware Cloud migration, preserving backups and resolving ambiguous copies safely.
8. Qualify the actual current SDK v0.42 Wine reader lease against a separate publisher process across full title lifetime, pending refusal, interruption/retry, close/crash release, and supported process behavior.
9. Link and deploy the actual normal production app with composed services; diagnostic-only and archive-only success do not satisfy this.
10. Verify real private title preparation/publication/registration/Ready, cold restart and interrupted retry, exact artifact/ownership gates, and revision consistency on PS5.
11. Run HL2 gameplay for at least 20 minutes, including audio, controller, save/restart, and pacing evidence. Only the rendered main menu is historically verified; gameplay remains untested.
12. Resolve release privacy/provenance blockers without replacing the retained runtime speculatively.
13. Obtain a fresh independent review of the complete integration branch and address findings. Old review agents are not a substitute for review of final composition; earlier agents hit spending limits.
14. Return to the app icon last. Assets were integrated, but old/missing artwork remains unresolved; do not alter native databases or reinstall titles blindly to clear artwork caches.

Keep evidence scoped to what actually ran, maintain the progress ledger, and leave the console restored/released at every idle boundary. The integration goal must remain incomplete until these product and hardware requirements are met.
