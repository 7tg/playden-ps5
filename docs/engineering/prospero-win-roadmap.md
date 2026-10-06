# prospero-win integration roadmap

Status: remaining-work roadmap for review, 2026-10-05. The per-game bottle/title
architecture is approved; Task 1 is complete. This document organizes Tasks 2–8
of the [implementation plan](../superpowers/plans/2026-10-05-prospero-win-integration.md).
It does not claim those tasks are implemented or their console gates have passed.

## Outcome

Install an entitled Windows Steam game through Playden, prepare its own Wine
bottle, register its own PS5 Home title, and launch the same effective configuration
from Playden or Home. Half-Life 2 (Steam app 220) is the first supported recipe.
Keep existing downloads and saves; unsupported games remain downloaded with a clear
compatibility status. Broader Wine compatibility is a later roadmap.

## Milestones and dependencies

| Milestone | Tasks / owner | Deliverable | Acceptance gate |
| --- | --- | --- | --- |
| M0 — Native contracts | Task 1 / Playden + helper; complete | Launch adapter, external provisioning, registration/query and supported manual removal path | Recorded repeated HL2 handoff, disposable registration/removal and original-artifact crash replay |
| M1 — Independent HL2 title | Tasks 2–3 / prospero-win, then Playden packaging | Per-game wrapper and reproducible runtime/template kit, pinned source submodule | A new owned HL2 title starts its default game from Home without arguments, renders gameplay with audio/controller input, and exits to Home |
| M2 — Install and Play through Playden | Tasks 4–6 / Playden | Durable identity/settings, production filesystem access, transactional preparation/registration, real Play action | Adopt existing HL2 without redownload; Playden and Home launch the same game/profile; interrupted preparation recovers without false readiness |
| M3 — Saves and Cloud | Task 7 / Playden + wrapper session records | Actual-bottle save mapping, prelaunch decisions and next-open reconciliation | Save/reload after restart, conflict/account/offline handling, Home-session reconciliation, no upload while game is active |
| M4 — Safe maintenance and release | Task 8 / Playden + reviewed removal mechanism | Settings, repair, staged update/rollback, save-preserving uninstall | Interrupted uninstall/update recover; reinstall preserves saves; final end-to-end console acceptance passes |

Execute M1 → M2 → M3 → M4. Task 4's portable records/settings work can begin after
the Task 2 descriptor contract is fixed; its production integration waits for the
validated kit. Task 5 requires Tasks 3–4, Task 6 requires Task 5, and Tasks 7–8
require the real launch/session contract. Each task gets its own tests, review,
commit and evidence. No calendar dates are promised before the hardware gates pass.

## What Task 1 established

See the [native contract](prospero-title-contract.md) and
[hardware evidence](../../results/20261005-prospero-contract/README.md).
Playden changes are committed as `df19870` and `e7b90a9`; the helper extension is
`e9f2cfc2c3913e42fdf83d74291e270b041ff0d9` on the patched Lapy base.

- The public SystemService launch path works after the exact-title filesystem/root
  grant. A returned `0x80940010` can precede ShadowMount's successful handoff;
  preserve the result and observe startup independently. No timer means Running.
- The grant changes the root view and can invalidate `/app0`. Production access
  must preserve title assets, trust store, credentials, service state and logs.
  The headless diagnostic is not a production bootstrap.
- ShadowMount's loopback API provides registration/query. Cache absence does not
  reserve a native title ID: inspect native install/source paths independently.
- A stale registration hook was restored by rerunning the installed service at
  the user's request. Production preparation must diagnose unavailable/stale state,
  retain its journal and stop; it must not silently restart console services.
- Native uninstall returned EBUSY while Playden was open. The verified after-close
  console removal path is manual; automated removal remains an M4 dependency.
- The tiny registration fixture failed module loading. M1 uses the real working
  wrapper and runtime; fixture registration is not proof that a game executable works.

## Source and artifact policy

Use a pinned `third_party/prospero-win` submodule for wrapper/tooling source and
separately hash-verified kits for runtime, initialized template and dependencies.
Do not compile Wine during ordinary Playden builds or vendor another Wine fork.

| Input | Baseline / rule |
| --- | --- |
| prospero-win source | `54caac176e86c60c7c407972583b491f67838ed9`; contains startup fix `824b4ec960b9ee93365437d7bb063db1d0e44b28` |
| Per-game wrapper | Task 2 produces a new committed descendant of that baseline; Task 3 pins the tested descendant |
| Tested Wine runtime | Retain artifacts from `e16ec8f3a19dec4a1162e7554ab73ce41f83bdb9`; do not rebuild them from the wrapper pin and call them identical |
| Patched Lapy | Base `611fa5f7ed46cdde7094b764168390ab5c4e89f8`, plus exact-FAKE build extension `e9f2cfc2c3913e42fdf83d74291e270b041ff0d9`; build for each actual title |
| Helper foundation | `9c0b994a048521af6fb84c73ded364504fe250e9`, SDK v0.42; record independently from Playden's SDK lock |
| Bottle preparation | Complete offline template with matching x32/x64 modules, DXVK 2.6.2 and repaired 32-bit MMDeviceEnumerator/DirectInput8 registrations |

Replace the downloaded helper **and its manifest** in packages. Verify title ID,
protocol, hashes, modes and firmware capability. Make required commits/artifacts
reachable from the chosen upstream/fork before claiming clean-checkout reproduction;
this roadmap does not authorize pushing or publishing them.

## Milestone work

### M1: Independent HL2 title

First add strict per-game descriptor parsing, title/data-root parameterization,
explicit local helper inputs, argument-free default-profile selection and exit-to-Home
upstream. Preserve generic prospero-win behavior without a descriptor. Persist
atomic startup/exit records before any launcher process disappears.

Then assemble the tested Wine/runtime assets and a clean offline prepared template
into a manifest-driven kit, including licenses, hashes, file modes and required
registry repairs. Add the source submodule and kit lock in Playden. Provision an
owned test title using existing HL2 files and a fresh private bottle; verify Home
launch, gameplay, input/audio and exit. Do not modify PPSA99995 or its working bottle.

### M2: Install and Play

Persist install/title/kit/recipe/settings identity, allocate from FAKE78000–FAKE78999
only after collision checks, and publish a one-entry profile. Establish production
access before native services/UI use paths that the grant would invalidate.

Prepare through a journal: validate inputs/space, stage files, preserve modes,
publish owned title/bottle/profile, request registration, independently reconcile
completion, then mark Ready. Adopt HL2's existing download in place. Cancellation,
missing media, stale service state and restart leave recoverable records and saves.

Connect live UI Play to the real launch service. Flush state and persist the pending
session before handoff. Reconcile native results, service handoff and wrapper startup
separately. Verify launch from both Playden and Home after reopen/retry. M2 uses
local saves; prepared-install Cloud is enabled only after M3 maps the actual bottle.

### M3: Saves and Cloud

Map Cloud to the actual per-game bottle, with backed-up migration of legacy paths.
Resolve conflicts/account confirmation before Play; allow an explicit offline play
choice for network failure. Reconcile wrapper sessions on the next Playden open,
including Home launches. Missing exit records mean interrupted/unknown; never upload
from an active bottle. Home launches initially use local saves without automatic sync.

### M4: Maintenance and release

Expose validated settings, preserve overrides and saves during repair, and stage
immutable kit updates with rollback. Block modification while a title is running.
Journal removal through source retirement, native unregister confirmation and
owned-file cleanup; retain allocation until completion and saves by default.

Before implementing automated unregister, verify whether the existing service has
a supported post-exit execution path. If it does not, produce a separate reviewed
design for a narrow helper/worker; do not silently add a persistent broker or treat
an EBUSY request as completion. This is an explicit gate for M4, not an M1 blocker.

## Release acceptance

Run host and upstream contract tests, native package build and focused hardware
checks at each milestone. Final acceptance adds at least 20 minutes of measured
HL2 gameplay, save/reload, Cloud conflict/offline/account handling, unavailable media,
repair/reinstall and failed-update rollback. Archive exact source/artifact identities,
logs and screenshots; build success, menu rendering and FPS overlays prove different
things and must be reported separately.

Next executable increment: **Task 2, per-game wrapper mode upstream**. Review the
remaining-task implementation details before starting code changes.
