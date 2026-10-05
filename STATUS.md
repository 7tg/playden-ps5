# Playden PS5 Launcher status and roadmap

Updated 2026-10-05. **The connected-library v1 (MVP) is functionally complete** and verified with a real account on the PS5 ([connected run](results/20261005-connected-ps5/README.md)); native 4K60 was removed from the MVP by the user. The selected direct **ps5-homebrew-ui** presentation is merged and production authentication, library, offline cache, search, and persistence are implemented. Next: v2 management UI, v3 real downloads, then the prospero-win runtime milestone.

The asynchronous artwork slice is committed on `main` (see the artwork row below). The PS5 MCP tools are available in the current session; claim the console before use.

Read [HANDOFF.md](HANDOFF.md) for exact worktree state, commands, ownership, and next steps. The [approved execution plan](docs/superpowers/plans/2026-10-04-foundation-to-connected-library.md) remains authoritative; the [execution ledger](docs/engineering/v1-progress.md) contains historical verification details.

## Completed implementation

| Area | Implemented behavior | Evidence and limits |
| --- | --- | --- |
| Pinned dependencies | ps5-native-app-boilerplate (tooling, `libc.prx`) and ps5-homebrew-ui as submodules under `third_party/` | Boilerplate at upstream latest; Hui at upstream latest plus unpushed local `feat/latin-glyphs` commits (see README) |
| Selected presentation and builds | Merged Home, Library, Details, account/settings, controller keyboard, focus/navigation, and management simulations; selected Mac/PS5 builds with pinned dependencies | Actual graphical host captures and local PS5 packages. Legacy RmlFocus remains a regression path, not the selected presentation |
| Native Steam transport | Native DNS, certificate-validated HTTPS/WSS, cancellation, timeout, and persistent transport probe | Earlier PS5 gate passed; [transport evidence](results/20261004-steam-network-ps5/README.md). This is not authenticated CM or real-account acceptance |
| Authentication | QR Begin/Poll/Renew, bounded worker events, private session persistence, restore/cancel/retry/sign-out, real QR encoding in selected UI | Synthetic protocol/state tests and graphical QR decoder checks. PS5-issued QR approval and real session restart remain pending |
| Owned library and offline cache | Authenticated GetOwnedGames client, bounded account-aware worker, existing-screen projection, account-scoped cache, cache-only startup after network-only renewal failure | Host service/controller tests and synthetic graphical journeys; no public profile or user API key required. Real-account retrieval on PS5 remains pending |
| Search and metadata | Pinned Unicode folding, stable name ordering, controller text entry, retained query/no-results recovery, filters restricted to available facts | Host query/model and selected graphical tests; general font coverage is not established |
| Organization and preferences | Account favorites, hidden games, collections, ratings, notes, query/filter/sort/selected identity; separate global reduced motion | Worker IO, bounded pending-load edit replay, explicit reset, visible failures/retry, final-input shutdown flush, ordered cache → organization → credentials deletion |
| Persistence navigation | Reconciles delayed saved selection with visible Home shelves/Library; preserves active editor target and local selection intent | Reviewed regressions for missing/hidden games, empty catalogs, unrelated pending edits, off-route restoration, and note editing |
| Asynchronous artwork | Background fetch of the app-ID-derived Steam CDN portrait, bounded JPEG/PNG decode, graphics-thread texture cache, timed retry of failed covers, placeholders on failure | Host 42/42, legacy 21/21, TSan-clean service test, inspected synthetic captures, public HTTPS smoke, PS5 package; [artwork evidence](results/20261004-artwork-host/README.md). Real covers on the console remain for the connected journey |
| Title glyph fallback | Characters outside the baked atlases draw a visible `?`; Greek fully covered; one face per string; Arabic unshaped, isolated forms, drawn LTR | Graphical test with titles outside the catalog, `?` required in every fallback face; [glyph evidence](results/20261004-glyph-fallback-host/README.md). Broad coverage awaits ps5-homebrew-ui `feat/unicode-text` |
| Authenticated Steam CM | Hello, logon with refresh token and per-install device identity, license list, package PICS; acquisition dates feed "Recently added" (cache schema 4, carried forward when CM fails); CM renewal fallback for access tokens | Host tests with protoc fixtures and a scripted transport; real-account run on the PS5 offered and applied "Recently added". See [connected run](results/20261005-connected-ps5/README.md) |
| v2 management UI (simulated) | Install, storage, space, queue, pause/resume/cancel/retry, verify, uninstall, failure and restart recovery on the deterministic simulator; unavailable in normal builds | Acceptance matrix and controller-only tour: [v2 evidence](results/20261005-v2-management-simulation/README.md) |
| Games storage | Per-drive install routing with a default drive chosen in Settings, games on an absent drive kept and flagged, live Downloads storage card, Details naming the drive | Host tests plus console run: the title's sandbox only sees console storage, so one drive is offered there. [Feature ports evidence](results/20261005-macos-feature-ports/README.md) |
| Notifications | Toasts for finished, failed (with reason) and removed installs, Cloud sync results and pending choices, controller loss/return; wait behind dialogs | Model tests; uninstall toast observed on the console |
| Log viewer | Install, Cloud saves, sign-in and launch logs on the device (newest 128 KB, UTC-stamped, install lines carry the app ID); a download's log shows only that game and offers Retry | Model and reader tests; real install log viewed on the console |
| Steam Cloud saves | On-demand sync from Details over CM `Cloud.*` service methods: `ufs` path mapping into the install and a prospero-win style prefix, content-based three-way merge, conflict and account-confirmation dialogs, backups before replacing local saves | Engine, service and model tests against a fake Steam Cloud; on the console the account's real Half-Life 2 saves downloaded (8 files) and a re-check was up to date. Uploads not run against the real account |

At `d237176`, the selected host suite passed **38/38**, legacy regression suite **21/21**, and selected PS5 compile/link/sign/package passed. The real graphical persistence restoration capture is in [persistence evidence](results/20261004-persistence-host/README.md). These results do not establish native lifecycle or physical-controller acceptance.

## Remaining roadmap

| Order | Work | Completion evidence |
| --- | --- | --- |
| 1 | v3: real Steam downloads and management | Done on the console: real depot download, licensed-depot selection, journal resume after interruption, install record/file list, managed uninstall. app 140 installed (87 MB) and Half-Life 2 (app 220) installed (1,471 files, 5.77 GB, 4 depots) with a verified pause/resume; both appear in the Recently Installed shelf. [v3 evidence](results/20261005-first-real-install/README.md) |
| 2 | prospero-win packaging and install design | Survey plus a console runtime attempt: [investigation](docs/engineering/prospero-win-investigation.md). Playden-side profile drafting implemented and tested (`src/runtime/prospero_profile.*`). Open decisions remain for the prospero-win owner (handoff, template prefix, `/data` access, Steamworks) |
| 3 | Runtime milestone: launch through prospero-win | Attempted with Half-Life 2 on 2026-10-05: the installed PW helper now completes, the Playden-generated profile loads, and a direct PW launch reaches Wine. It exits because `/data/prospero-win/prefixes/playden` is missing. Playden’s title-launch probe separately crashes before handoff. No gameplay established. [Baseline evidence](results/20261005-hl2-runtime-baseline/README.md) |
| — | Native 4K60 performance | Removed from the MVP by the user (2026-10-05); kept as a separate, unscheduled performance goal |

The [macOS feature ports](docs/superpowers/plans/2026-10-05-macos-feature-ports.md) (games storage, notifications, log viewer, Steam Cloud saves) are implemented; Cloud sync around a game launch waits for the runtime handoff. Still unported: runtime-dependent per-game settings, profiles, display and input; first-run setup; reset-app-data UI; ambient backdrop; other stores.

Installation/download/session management screens currently provide explicit development simulations. Real installation and Wine/runtime integration are not implemented yet. The Wine implementation will be `../ps5-homebrew/prospero-win`; roadmap step 2 covers how downloaded games are packaged and installed for it.

## Working constraints

Work on `main`, commit coherent increments, use the agent-team skill and independent review, and preserve the selected UI. Do not rebuild the old RmlFocus presentation or restore its stashed WIP. Keep reusable selected-renderer fixes in ps5-homebrew-ui and product behavior here.

The launcher title is **FAKE77031**. Coordinate with the runtime effort before using the shared PS5. The PS5 MCP is available: claim the console before use, refresh the claim during long waits, and release it when done. A successful build or upload is never console acceptance.
