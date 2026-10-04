# Playden PS5 Launcher PRD and Roadmap

Written 2026-10-04. Product direction agreed with Leo; this written specification is awaiting review. Detailed acceptance scenarios and architectural boundaries below are proposed implementation requirements, not claims of completed work.

> **Implementation decision update (2026-10-04):** The user selected direct ps5-homebrew-ui, merged in `3bc1e38` with follow-up fixes/evidence through `703dd1f`. This supersedes the RmlFocus-specific framework and ownership choices below; product behavior, direct PS5 Steam access, and separation from Wine remain unchanged. The [updated execution plan](../plans/2026-10-04-foundation-to-connected-library.md) describes integration into the selected screens. Historical RmlFocus evidence is retained. Performance work is deferred by the user, and native 4K60 acceptance remains open.

> **Runtime decision update (2026-10-05):** The PS5 Wine runtime is `../ps5-homebrew/prospero-win`. A follow-up step investigates it and documents how games downloaded from Steam and other stores are packaged and installed for it, before install/runtime integration begins (see `STATUS.md`).

## Product purpose

Playden brings the macOS Playden library and couch experience to a jailbroken PS5. A player connects Steam, browses games with a DualSense, and eventually installs and launches Windows games through the separately developed PS5 Wine runtime.

The first useful release is a connected library. Game installation and management UI follows before real installation. Progress on these journeys must not depend on Wine being ready.

## Agreed decisions

- Adapt Playden to PS5-style navigation rather than reproducing the macOS interface unchanged.
- Connect to Steam directly from the PS5. Display a QR code on the TV for approval through the player's phone; no Mac or PC companion service is required for normal use.
- Use RmlFocus as the preferred UI framework. We own it and can make the improvements Playden requires.
- Require native 3840 × 2160 UI rendering at 60 Hz for MVP acceptance. An upscaled 1080p interface does not satisfy this requirement. Lower-resolution intermediate effects such as blur are acceptable when final UI geometry and text remain native 4K.
- Build and validate game-management journeys using a simulated backend before implementing actual installation or connecting Wine.
- Keep Wine development in its separate repository and schedule launcher integration independently.

## Audience and success

The initial user is a player with an existing Steam library and a jailbroken PS5 connected to a TV. Once Playden is installed, the supported journeys require a DualSense and a phone for Steam sign-in approval, without a desktop companion.

MVP succeeds when the player opens Playden, signs in, browses their real library, opens game details, searches or filters games, and returns to their previous position smoothly at native 4K60. Restarting restores the account session when still valid and retains cached library data. Network and authentication failures expose a clear recovery action.

Library ownership, files downloaded, runtime preparation, and playability are separate facts. Presence in the library never implies verified compatibility.

## Ownership boundaries

| Repository | Owns |
| --- | --- |
| Playden PS5 launcher | Product screens, navigation structure, Steam account/session handling, library metadata, artwork requests, local preferences, job models, installation orchestration, and later launch orchestration |
| RmlFocus | Rendering, layout, focus primitives, reusable controls, text and image rendering, bounded collections, sheets/dialogs, text entry, and frame diagnostics |
| PS5 Wine runtime | Windows execution, runtime capabilities, game preparation mechanisms, compatibility, and runtime lifecycle implementation |

Playden requests runtime operations through an explicit interface. The exact transport and process model are chosen with the runtime project once its supported interface exists. No runtime transport or process-creation capability is assumed by this PRD.

The macOS app's launcher branch in `/Users/leolobato/Documents/Projetos/Personal/GameNative/playden` is the reference implementation for journeys, branding, assets, and Steam behavior. The user specified `feature/launcher`; inspection found no local ref with that exact name, but found `feature/launchers` checked out at `de952b3`. Use `feature/launchers` provisionally as the intended reference, with this naming discrepancy explicit. Inspect that branch rather than defaulting to `main`, and record the exact revision used for each porting decision as it evolves.

SwiftUI, Apple input APIs, CrossOver commands, and macOS storage assumptions are not part of the PS5 architecture. Reuse of SteamKit source is subject to a portability review; reuse of protocol knowledge and fixtures does not require keeping Swift.

## User journeys

### First run and Steam sign in

Show the Steam QR challenge, pending approval, expiration, refresh, cancellation, and retry states. Approval transitions to library synchronization. Keep authentication work off the rendering thread. A failed connection must not trap the user in a spinner.

Persist session material through a platform storage adapter, exclude credentials and tokens from logs, and clear local session material on sign-out. Invalid or revoked sessions lead back to sign-in. The QR view displays a real Steam authentication challenge; a generic browser link or fixture QR does not satisfy acceptance.

### Home and Library

Home provides a useful entry to the library without inventing local play history. Library supports artwork, game names, search, sorting, and filters appropriate to available data. Game details expose metadata and action availability.

Keep a cached library available offline, identify stale data, and provide refresh/retry. Provide explicit loading, empty library, no search results, unavailable artwork, and network-error states. Artwork failures must not prevent navigation.

Search includes controller-operated text entry. International game names and long titles are acceptance inputs. Font coverage must be validated; no blanket claim of complete language support follows from basic Unicode rendering.

### PS5 navigation

- L1/R1 switches primary sections, with the selected section and focus visibly distinct.
- Cross activates; Circle dismisses the topmost layer or returns one level.
- Options opens game actions where available.
- Returning from details restores the selected game and scroll position.
- Dialogs and sheets own focus until dismissed, then restore their opener.
- Refreshing, filtering, or removing an item preserves focus by stable identity or selects a predictable neighboring item.
- Every screen presents the relevant controller hints. Focus borders, restrained scaling, and readable TV typography define the initial visual direction.

Exact section labels and layouts will be settled during screen design. They must not change the navigation and focus-restoration contract above.

### Game management before real installation

The v2 development simulation supports the complete journey: choose Install, select storage, review required space, queue work, inspect progress, pause/resume/cancel/retry, verify files, and confirm uninstall.

The simulator is deterministic and supports interrupted jobs, insufficient space, storage becoming unavailable, transfer failures, verification failures, and restart recovery. It exercises the same presentation-facing contracts intended for the real backend. It must not represent synthetic progress or storage as real operations in normal builds.

Until real operations exist, normal builds explain unavailable actions. Simulated state is isolated from real account/job data and clearly identified in development mode.

### Real installation and later play

Real management downloads owned game files, persists job state, verifies content, and removes only the selected managed installation. Interrupted operations reconcile disk and job state after restart. Cancellation and uninstall must have explicit cleanup behavior and must preserve unrelated files and saves.

Represent transfer state independently from runtime readiness. A game may be downloaded while preparation is unavailable. Runtime integration later adds capability detection, preparation requests, launch, running status, exit, and actionable failures. Returning from a session restores Playden navigation.

## Architecture and data flow

Use three boundaries inside the launcher:

1. Presentation renders state and emits user actions through RmlFocus.
2. Application services manage account, library, jobs, preferences, and later sessions.
3. Adapters implement Steam, persistence, storage, platform services, and later runtime operations.

Steam events update application state; the UI consumes bounded updates on its own thread. Network requests, disk operations, decoding, and expensive metadata processing must not block rendering. Texture uploads also need a measured frame budget.

Stable game identifiers connect library entries, artwork, selected focus, and local installation records. Job identifiers survive restart. Store raw progress separately from its presentation so simulated and real jobs share UI behavior.

Keep the simulator behind service interfaces rather than duplicating screens. Avoid building a universal abstraction for multiple renderers: the selected presentation implementation uses RmlFocus directly.

## Native 4K60 acceptance

RmlFocus's recorded 1080p60 results establish a starting point. Its recorded 4K30 stress result leaves an unresolved performance gap. Newer components are documented as host-verified with console acceptance pending. None of those records proves Playden meets native 4K60.

Foundation work must measure representative Playden scenes before broad screen implementation. Investigate the measured costs of layout, clipping, effects, text, artwork uploads, and presentation before choosing renderer changes. Framework fixes belong in RmlFocus.

Required console scenarios:

- Browse and repeatedly scroll a proposed 1,000-entry fixture library, with long and international names.
- Deliver artwork while navigating and show placeholders/failures without layout jumps.
- Open details, an install sheet, and a confirmation; dismiss each with correct focus restoration.
- Update a simulated download queue while interacting with it.
- Exercise search text entry and account/error screens at native resolution.
- Run a sustained session and repeated clean launch/exit checks without growing memory use or lost focus.

Capture render resolution, refresh mode, frame-time percentiles, maximum frame time, missed presentations, memory behavior, build revision, and the tested console environment. Review controller feel on the physical DualSense as well as automated navigation.

The target frame interval is approximately 16.67 ms. Proposed gate: no sustained reduction below 60 Hz and no recurring interaction-induced misses in the representative scenarios. Isolated startup or system interruptions must be reported separately, not hidden in an average. Final test duration and numerical tail-latency thresholds will be defined in the implementation plan before acceptance runs.

## Release roadmap

| Stage | Deliverable | Done when |
| --- | --- | --- |
| Foundation | Reproducible PS5 application shell, RmlFocus integration, PS5 visual/navigation direction, native 4K60 proof, platform storage and diagnostics | Representative scenes meet the console gate; app opens, navigates, persists a setting, and exits reliably |
| MVP / v1 | Direct Steam QR sign-in, session restoration, real library/artwork, Home, Library, details, search/filtering, account/settings, offline/error recovery | A player completes sign-in and library journeys on PS5 at native 4K60; real authentication and connectivity are verified on console |
| v2 | Complete install and management UI backed by development simulation | All queue, storage, confirmation, recovery, and restart scenarios are controller-accessible and testable without Wine or real installation |
| v3 | Real Steam game downloads and management | Owned game files download, jobs recover, content verifies, and uninstall respects managed boundaries; runtime availability is reported independently |
| Independent runtime milestone | Wine capability/preparation/launch integration | A runtime-validated game repeatedly launches, exits, and returns to Playden with useful failure diagnostics |

Steam transport/authentication feasibility should be investigated during Foundation in parallel with UI performance work where practical. This is sequencing guidance, not a requirement to delegate to another agent. A UI proof alone does not establish Steam connectivity in the native-title environment.

The runtime milestone can land when the other project is ready, using externally prepared games if necessary. It must not block v1 or v2. No release date depends on an assumed Wine completion date.

## Deferred scope

Steam Cloud synchronization requires its own conflict, offline, and recovery specification and is not silently included in v3. Other stores, achievements, social features, DLC management, an in-game overlay, automatic game updates, and broad compatibility promises are outside these initial milestones.

The launcher does not implement the jailbreak, Wine internals, graphics translation, or fixes for individual Windows games.

## Validation strategy

Host checks cover job transitions, persistence/recovery, service adapters with fixtures, stable focus, navigation stacks, and screenshots. Use injected failures to verify visible recovery and avoid tests that merely repeat implementation details.

Console checks establish native 4K rendering, frame delivery, controller feel, network/TLS behavior, authentication, filesystem permissions, lifecycle, and later runtime integration. A successful build, upload, or host screenshot does not establish console acceptance.

## Decisions to resolve during implementation planning

These are feasibility or delivery choices, not changes to the agreed product scope:

- Select the PS5 Steam client implementation after reviewing macOS SteamKit portability and native-title networking requirements.
- Confirm the supported console/firmware baseline; sibling evidence currently targets 13.60, but this PRD does not claim wider support.
- Establish persistent credential/cache paths and, later, permitted game-storage locations with console evidence.
- Define exact performance run durations and tail-latency limits, and choose font coverage for acceptance fixtures.
- Pin the RmlFocus revision and record any framework changes required by the 4K60 gate.
- Agree on the runtime contract with its owner when integration becomes actionable.

## Local reference material

Paths below are sibling working copies used during planning, not build dependencies:

- macOS Playden: the launcher branch identified above, including `../playden/docs/prd/` and `../playden/Packages/SteamKit/README.md`; inspect branch contents rather than assuming these documents fully describe its implementation.
- RmlFocus status and evidence: `../rmlfocus/docs/plan/roadmap.md`, `../rmlfocus/docs/design/launcher-sample.md`, and `../rmlfocus/docs/design/content-api.md`.
- Alternative UI evaluation: `../rmlfocus/results/20261004T235331Z-hui-playden/README.md` and `../ps5-homebrew/ps5-homebrew-ui/docs/ADOPTING.md`.
- Wine project context: `../playden-ps5/README.md`; its runtime progress is independent of this roadmap.
