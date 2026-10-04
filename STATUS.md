# Playden PS5 Launcher status and roadmap

Updated 2026-10-04. The connected-library v1 is in progress on `main`. The selected direct **ps5-homebrew-ui** presentation is merged and production authentication, library, offline cache, search, and persistence are implemented. Real-account console acceptance remains open. **4K performance work is deferred by user direction.**

The latest completed implementation commit is **`d237176` — Bind live organization and preferences to selected launcher**. An asynchronous artwork implementation is present in the working tree but is **not committed or finally reviewed**. Preserve it when resuming.

Read [HANDOFF.md](HANDOFF.md) for exact worktree state, commands, ownership, and next steps. The [approved execution plan](docs/superpowers/plans/2026-10-04-foundation-to-connected-library.md) remains authoritative; the [execution ledger](docs/engineering/v1-progress.md) contains historical verification details.

## Completed implementation

| Area | Implemented behavior | Evidence and limits |
| --- | --- | --- |
| Selected presentation and builds | Merged Home, Library, Details, account/settings, controller keyboard, focus/navigation, and management simulations; selected Mac/PS5 builds with pinned dependencies | Actual graphical host captures and local PS5 packages. Legacy RmlFocus remains a regression path, not the selected presentation |
| Native Steam transport | Native DNS, certificate-validated HTTPS/WSS, cancellation, timeout, and persistent transport probe | Earlier PS5 gate passed; [transport evidence](results/20261004-steam-network-ps5/README.md). This is not authenticated CM or real-account acceptance |
| Authentication | QR Begin/Poll/Renew, bounded worker events, private session persistence, restore/cancel/retry/sign-out, real QR encoding in selected UI | Synthetic protocol/state tests and graphical QR decoder checks. PS5-issued QR approval and real session restart remain pending |
| Owned library and offline cache | Authenticated GetOwnedGames client, bounded account-aware worker, existing-screen projection, account-scoped cache, cache-only startup after network-only renewal failure | Host service/controller tests and synthetic graphical journeys; no public profile or user API key required. Real-account retrieval on PS5 remains pending |
| Search and metadata | Pinned Unicode folding, stable name ordering, controller text entry, retained query/no-results recovery, filters restricted to available facts | Host query/model and selected graphical tests; general font coverage is not established |
| Organization and preferences | Account favorites, hidden games, collections, ratings, notes, query/filter/sort/selected identity; separate global reduced motion | Worker IO, bounded pending-load edit replay, explicit reset, visible failures/retry, final-input shutdown flush, ordered cache → organization → credentials deletion |
| Persistence navigation | Reconciles delayed saved selection with visible Home shelves/Library; preserves active editor target and local selection intent | Reviewed regressions for missing/hidden games, empty catalogs, unrelated pending edits, off-route restoration, and note editing |

At `d237176`, the selected host suite passed **38/38**, legacy regression suite **21/21**, and selected PS5 compile/link/sign/package passed. The real graphical persistence restoration capture is in [persistence evidence](results/20261004-persistence-host/README.md). These results do not establish native lifecycle or physical-controller acceptance.

## Artwork work in progress

The working tree adds a background artwork service, bounded JPEG/PNG decoder, graphics-thread texture cache, renderer wiring, platform lifetime management, and four tests. It fetches the reference app's portrait-cover URL derived from Steam app ID. Simulation retains its fixed fixture artwork; unavailable production covers retain placeholders.

Implemented limits:

- 64 subscriptions; keys include account generation, app ID, and source identity.
- 10-second HTTP deadline and 4 MiB transfer limit; verified HTTPS, exact canonical source, no redirects.
- Maximum decoded dimensions 1024×1024 and 4 MiB RGBA; a separate 16 MiB internal decoder allocation cap.
- 16 MiB queued decoded results; cancellation, stale-result rejection, and suppression of retries on every frame.
- 64 textures / 64 MiB GPU cache; at most two uploads / 8 MiB per frame; eviction protects textures referenced by the current frame.

Observed verification for this uncommitted work:

- Selected host build passed; artwork-focused CTest **4/4** passed, including the actual graphical consumer.
- Public Steam-cover HTTPS smoke passed: **300×450**, **540,000 RGBA bytes**.
- Local PS5 compile/link/sign/package passed; eboot SHA-256 `859c9bd9de7dbc30ac98e17594a4d067e8d318e16cf729a52608a6ef905732e0`.
- Early independent source review found no concrete blocker, but **final review is outstanding**.
- The expanded host suite contains **42 tests**; the entire 42-test suite has **not** been run for this slice. New captures have been generated but not visually inspected or committed.
- An unexpected exception from an injected ArtworkBackend currently escapes the worker. Decide and test the exception contract before final review; PS5 builds use exceptions disabled.

## Remaining roadmap

| Order | Work | Completion evidence |
| --- | --- | --- |
| 1 | Finish the current artwork slice | Resolve the exception-contract review point, run the full selected suite, inspect delayed/ready/failure captures, obtain final independent review, rebuild/package, record evidence and commit |
| 2 | Dynamic title glyph coverage, remaining 4A.3 | Bounded coverage beyond fixture subsets or explicit unsupported-glyph fallback, mixed-script graphical checks; Arabic shaping/BiDi limitations must remain explicit |
| 3 | Authenticated Steam CM and backend acceptance | Implement/verify authenticated CM and reconcile the remaining entitlement/PICS requirement with the committed backend decision; transport success and GetOwnedGames alone do not close this gate |
| 4 | Real connected journey and native functional acceptance | Coordinate shared PS5 access; direct QR phone approval, restoration, real library comparison, offline/retry, account switch/sign-out, preferences restart, artwork, clean exit, and physical DualSense checks |
| 5 | Native 4K60 performance acceptance, deferred | Only resume when the user lifts the deferral. Measure the actual selected consumer and required scenarios; native 3840×2160 at 60 Hz remains an unverified MVP requirement |

Installation/download/session management screens currently provide explicit development simulations. Real installation and Wine/runtime integration remain outside this v1 scope; another effort owns the runtime.

## Working constraints

Work on `main`, commit coherent increments, use the agent-team skill and independent review, and preserve the selected UI. Do not rebuild the old RmlFocus presentation or restore its stashed WIP. Keep reusable selected-renderer fixes in ps5-homebrew-ui and product behavior here.

The launcher title is **FAKE77031**. Coordinate with the runtime effort before using the shared PS5. No Chauffeur inbox or PS5 MCP tool was available in this session, and no console action was attempted during persistence/artwork integration. A successful build or upload is never console acceptance.
