# Playden PS5 Launcher agent handoff

Updated 2026-10-04 at the user's request. Resume the existing connected-library v1 objective; do not restart implementation or treat the artwork slice as the whole goal. Read [STATUS.md](STATUS.md) first for completed work and the roadmap.

## Repository and instructions

- Checkout: `/Users/leolobato/Documents/Projetos/Personal/GameNative/playden-ps5-launcher`.
- Branch: `main`; latest completed implementation commit: **d237176**. A later documentation-only commit may be HEAD.
- Read applicable repository instructions, `README.md`, [approved plan](docs/superpowers/plans/2026-10-04-foundation-to-connected-library.md), [design](docs/superpowers/specs/2026-10-04-playden-ps5-launcher-design.md), and [execution ledger](docs/engineering/v1-progress.md).
- The user authorized execution and incremental commits on main. Do not ask again for ordinary implementation or commits.
- Use `agent-team:agent-team`. The lead owns contracts/integration/final verification. Delegate independent work with exclusive ownership, sequence dependencies, and independently review meaningful changes. Skill roles used here: workers **gpt-5.6-sol / medium**, reviewer **gpt-5.6-sol / high**.
- Selected UI is direct **ps5-homebrew-ui**, merged in `3bc1e38` with fixes/evidence through `703dd1f`. Preserve its screens. Earlier RmlFocus-specific presentation instructions are superseded.
- **No 4K performance profiling, MSAA experiments, RSS investigations or soaks.** Performance work is deferred, not accepted or waived. Keep native 3840×2160 output.
- Runtime/Wine is independently owned. Do not implement real installation or runtime integration here.

Reference checkout: `../playden`, confirmed branch `feature/launchers`, inspected revision `de952b3d49167e635945ada88cd8533e07d40d44`. Use `git show`/`git grep` against the revision; never switch or disturb that checkout. Selected framework/examples: `../ps5-homebrew/ps5-homebrew-ui` and `../ps5-homebrew`. Historical framework: `../rmlfocus`. Native toolchain reference: `../playden-ps5`.

## Preserve this uncommitted work

The artwork implementation is intentionally left in the shared working tree. The handoff documentation commit does not include it. Begin with `git status --short` and inspect the actual diff; do not reset, clean, stash, overwrite, or recreate these files.

| Files | Responsibility and state |
| --- | --- |
| `src/homebrew/artwork.{hpp,cpp}` | New bounded background service; diagnostics worker, frozen |
| `src/homebrew/artwork_backend.{hpp,cpp}` | New canonical HTTPS client and bounded JPEG/PNG decoder; diagnostics worker, frozen |
| `tests/homebrew_artwork{,_backend}_test.cpp` | New service/decoder tests and optional public HTTPS smoke |
| `src/homebrew/artwork_cache.{hpp,cpp}` | New graphics-thread cache with injectable upload/release backend; build_platform worker, frozen |
| `tests/homebrew_artwork_cache_test.cpp` | New stale-result, limits, upload, eviction, failure and lifecycle tests |
| `src/homebrew/{view.cpp,render.hpp}` | Lead integration into the actual renderer, optional ArtworkService pointer, live cache diagnostics |
| `src/homebrew/{host.cpp,ps5.cpp}` | Lead production construction, shutdown, and GL lifetime wiring |
| `src/homebrew/simulation.{hpp,cpp}` | Lead adds account-generation counter, incremented when account-specific state clears |
| `tests/homebrew_artwork_graphical_test.cpp` | Lead actual-renderer delayed/ready/failed artwork and same-account replacement test |
| `cmake/HomebrewUI.cmake`, `cmake/HomebrewPS5.cmake` | Lead new targets/tests and native manual link/package dependencies |

No known task processes remain running. Root host build/test handle `40961` was polled to exit 0 during handoff. Native build handle `15860` had already exited 0. Workers report no live handles and are frozen. Revalidate any process you discover rather than assuming a stale handle represents live work.

Native agents used in this session, if still available: `/root/diagnostics`, `/root/build_platform`, `/root/plan_review`. The reviewer supplied **early source review only**, not final clearance. Do not assume a new session can address these old agents.

## Artwork contracts and decisions

Namespace `playden::homebrew`:

- `ArtworkKey`: account generation, GameId, exact source string. `ArtworkImage`: width, height, owned RGBA vector. `ArtworkResult`: key and optional image.
- `ArtworkBackend::Load(key, atomic cancel)` runs only on the service worker. `HttpArtworkBackend` creates its existing HttpClient lazily on that worker.
- `ArtworkService::ReplaceSubscriptions(vector)` replaces the complete visible missing-key set without waiting for IO. Invalid, duplicate or excessive replacements are rejected atomically. Continuously subscribed keys get one attempt; leaving/reentering permits retry. Removal cancels active work and removes queued/results; stale completions are discarded. `Pump()` defaults to two results. Shutdown cancels and joins.
- `LiveArtworkCache::BeginFrame(generation)`, `TextureFor(id,source)`, `EndFrame()` run on the graphics thread. Ready textures are reused; only missing visible keys subscribe. EndFrame creates at most two textures and evicts only textures not referenced by the current frame. Generation change/destruction releases resources. Backend/service outlive the cache.
- `Render(assets, ArtworkService* = nullptr)` preserves fixture behavior for existing simulation callers. Real Mac/PS5 startup injects the service and does not load the nine fixture textures. After collecting draw commands, EndFrame uploads ready art; new textures appear on the next frame. The current draw list's existing textures remain protected.
- Production portrait source is `https://cdn.cloudflare.steamstatic.com/steam/apps/<app_id>/library_600x900.jpg`, matching the pinned macOS reference in `App/PlaydenApp.swift`. It is deliberately derived from app ID, rather than stretching the tiny GetOwnedGames icon. The HTTP backend allows only that exact canonical URL matching the key. Missing covers retain placeholders; no fallback host/redirect is silently accepted.
- JPEG/PNG decoding uses the already pinned stb header, with a private static implementation. Dimensions are checked before pixel decode. A custom aligned allocator caps stb's internal allocations at 16 MiB to bound malicious PNG expansion; RGBA output adds at most 4 MiB. Do not replace this with dimension checking alone.
- Queue/GPU/upload limits are detailed in STATUS.md and public header constants. These are implementation bounds, not measured 4K performance evidence.

## Verified and pending checks

Committed persistence baseline `d237176`: selected host **38/38**, legacy **21/21**, graphical persistence inspection and local PS5 package passed; final independent review cleared it. Relevant evidence: `results/20261004-persistence-host/`.

Current artwork WIP:

- Root host build passed and artwork-focused CTest **4/4** passed, including `homebrew_artwork_graphical`.
- Root repeated the public HTTPS smoke during handoff: `artwork 300x450 540000 bytes`.
- Root selected PS5 compile/link/sign/package passed. Package: `build/homebrew-ps5/dist/FAKE77031`; eboot hash: `859c9bd9de7dbc30ac98e17594a4d067e8d318e16cf729a52608a6ef905732e0`.
- Workers report focused tests and repeated service/backend/cache runs passing. These reports supplement, rather than replace, root's checks above.
- The full selected configuration now registers **42 tests**, but all 42 have not been run together for artwork. Legacy was last verified at the committed persistence baseline.
- Generated graphical captures: `build/homebrew-host/synthetic-live-artwork/pending.png` and `ready.png`. They use synthetic striped covers and one deliberate failure. They were not visually inspected or copied into committed evidence before the handoff.
- Early independent source review found no concrete blocker in cancellation, limits, stale identities, texture lifetime, or platform teardown. Final review remains outstanding.

Local logs, useful while this machine retains them:

- `/tmp/playden-artwork-build.log`
- `/tmp/playden-artwork-tests.log`
- `/tmp/playden-artwork-ps5.log`

Do not treat those logs or the package hash as proof that future edits have been verified.

## Immediate resume sequence

1. Read the current diff and the files above; preserve the implementation and identify any changes made after this handoff.
2. Resolve the outstanding backend exception contract. `ArtworkBackend::Load` is not `noexcept`, and an unexpected exception currently escapes `ArtworkService::Run`, terminating the process. The worker reproduced this with an injected throwing backend, then removed the unfinished test when asked to freeze. Normal transport/decode errors use `nullopt`; PS5 compilation disables exceptions. Decide whether to explicitly require a nonthrowing contract or catch unexpected backend exceptions where supported, and add the corresponding behavioral test. Do not claim this issue is already resolved.
3. Run the selected build and full 42-test suite. Inspect the actual graphical pending/ready captures for correct game images, fixed-size failure placeholders, focus and layout. The current graphical test asserts navigation during worker delay and invalidation even when sign-out/sign-in to the same account occurs between render frames.
4. Finish independent review of the final artwork diff, fix concrete findings, rebuild the PS5 package after any changes, and record current artifact hashes. No console deployment is needed for this host/build completion step.
5. Add sanitized evidence under a new artwork results directory, update STATUS.md/plan/ledger, and commit the coherent artwork slice on `main`. Keep dynamic title glyph coverage unchecked until separately implemented and verified.
6. Continue the remaining roadmap in STATUS.md: dynamic glyph coverage, authenticated CM/backend acceptance, connected journey and coordinated native functional checks. Keep deferred performance acceptance open.

Commands from the repository root:

```sh
cmake --build --preset homebrew-host -j 4
ctest --preset homebrew-host --output-on-failure
ctest --preset homebrew-host -R artwork --output-on-failure
build/homebrew-host/homebrew_artwork_backend_test --live-smoke probes/steam-network/.deps/host/cacert.pem
scripts/build-homebrew-ps5.sh
cmake --build --preset legacy-host -j 4
ctest --preset legacy-host --output-on-failure
git diff --check
```

Use legacy checks when the final changes affect shared code; they do not substitute for testing the selected graphical consumer. Avoid rerunning broad checks without a new change or unresolved concern.

## Existing production integration to retain

Normal startup constructs real AuthClient/SteamService, LibraryClient/LibraryService/cache, and PersistenceService/controller. Simulation is explicit and isolated; normal captures/replays that could leak real QR challenges are rejected. Secrets stay in SessionStore; presentation events and evidence exclude credentials/challenge URLs.

Host data defaults to `build/homebrew-host/playden-data` and supports `--data-dir`. PS5 data uses `/app0/playden-data`; CA bundle is `/app0/trust/cacert.pem`. Host auth and library use separate LocalSessionRepository instances because preparation is mutable. Keep service/backend/repository lifetimes ordered correctly.

Persistence sign-out barrier is **library cache → organization → credentials**. Failed deletion retains account identity and permits retry. Do not regress final-input shutdown saves, bounded pending-load edit replay, collection-ID collision handling, global preferences surviving account changes, or active note-editor target preservation. Organization files are private; reduced-motion preferences use ordinary atomic platform writes.

Real owned-library retrieval uses authenticated GetOwnedGames with stored token, not a public profile or user API key. Network-only renewal failure permits trusted cache-only startup; revoked/rejected credentials do not. Unsupported install/runtime facts remain unavailable in normal mode.

## Console coordination and remaining acceptance

Title **FAKE77031** belongs to this launcher. The user previously said the PS5 was available via MCP with zftpd, but shared access must still be coordinated with the runtime effort before use. Chauffeur inbox and PS5 MCP tools were unavailable in this session; do not infer peers, credentials or exclusive access from files. Discover available tools again in the new session.

No console deployment happened during this persistence/artwork work. Remaining hardware checks include PS5-issued QR approval, real-account restoration/library comparison, offline recovery and deletion/retry, native preferences/filesystem lifecycle, asynchronous artwork, clean exit, and physical DualSense navigation. A successful build or upload does not establish acceptance.

Authenticated CM is still unimplemented/unproven beyond the earlier native WSS transport gate. Start from `docs/engineering/steam-backend-decision.md` and `steam-reference.md`; inspect the pinned reference CM/entitlement/PICS sources. Do not silently remove that requirement because GetOwnedGames already returns a library.

The initial CJK/Arabic atlases are fixture subsets. Broader title coverage, mixed scripts, and Arabic shaping/BiDi limitations remain unresolved. Full v1 completion must also retain the native 4K60 requirement, which cannot be accepted while the user's performance deferral is active.
