# Playden PS5 Launcher status and roadmap

Updated 2026-10-04. The connected-library v1 is in progress on `main`. The selected direct **ps5-homebrew-ui** presentation is merged and production authentication, library, offline cache, search, and persistence are implemented. Real-account console acceptance remains open. **4K performance work is deferred by user direction.**

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

At `d237176`, the selected host suite passed **38/38**, legacy regression suite **21/21**, and selected PS5 compile/link/sign/package passed. The real graphical persistence restoration capture is in [persistence evidence](results/20261004-persistence-host/README.md). These results do not establish native lifecycle or physical-controller acceptance.

## Remaining roadmap

| Order | Work | Completion evidence |
| --- | --- | --- |
| 1 | Finish the connected journey on the console | Done on the PS5: QR approval, real library, covers, CM dates. Remaining: restart restoration with the fixed build, offline/retry, account switch/sign-out, preferences restart, clean exit, physical DualSense. Coordinate shared access; keep the claim refreshed |
| 2 | Investigate prospero-win as the Wine implementation | `../ps5-homebrew/prospero-win` is the selected Wine implementation (user direction, 2026-10-05). Investigate the repository and document how games downloaded from Steam (and other stores) should be packaged and installed for it, before any install/runtime integration |
| 3 | Native 4K60 performance acceptance, deferred | Only resume when the user lifts the deferral. Measure the actual selected consumer and required scenarios; native 3840×2160 at 60 Hz remains an unverified MVP requirement |

Installation/download/session management screens currently provide explicit development simulations. Real installation and Wine/runtime integration are not implemented yet. The Wine implementation will be `../ps5-homebrew/prospero-win`; roadmap step 2 investigates how downloaded games are packaged and installed for it.

## Working constraints

Work on `main`, commit coherent increments, use the agent-team skill and independent review, and preserve the selected UI. Do not rebuild the old RmlFocus presentation or restore its stashed WIP. Keep reusable selected-renderer fixes in ps5-homebrew-ui and product behavior here.

The launcher title is **FAKE77031**. Coordinate with the runtime effort before using the shared PS5. The PS5 MCP is available: claim the console before use, refresh the claim during long waits, and release it when done. A successful build or upload is never console acceptance.
