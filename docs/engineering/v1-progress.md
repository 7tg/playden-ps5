# V1 execution ledger

Objective: implement the complete connected-library v1 from the approved Foundation to Connected Library plan, including console performance and real Steam acceptance. User expanded scope after milestone 1 and directed work and incremental commits on `main`.

Current direction: resume the approved connected-library v1 on `main` after selection of the direct **ps5-homebrew-ui** presentation, merged in `3bc1e38` and fixed/documented through `703dd1f`. This supersedes the previous UI pause and RmlFocus presentation ruling. Preserve the merged visuals and simulation regression evidence; connect real services through their existing state/projection boundary. **4K performance work remains deferred.**

The updated [execution plan](../superpowers/plans/2026-10-04-foundation-to-connected-library.md) is authoritative for sequencing and file ownership.

| Task | State | Next evidence / work |
| --- | --- | --- |
| 1A–1B | Historical RmlFocus shell complete | Retain results/20261004T132145Z-shell; selected consumer needs its own lifecycle/storage validation |
| S1 | Complete build integration, independently reviewed | Selected host 21/21, legacy 18/18, actual graphical Home capture unchanged, local PS5 package/CA verified; startup service wiring remains 4A.1 |
| 2A | Selected Hui UI merged; host-verified simulations and local PS5 package | Reuse existing screens, focus, keyboard, filters and management simulations; do not rebuild them in RML |
| 2B | Deferred by user; not passed | No profiling, MSAA, RSS or soak work; eventual selected-renderer native 4K60 acceptance remains open |
| 3A | Complete on PS5 | Native DNS, cancellation, timeout, verified HTTPS/WSS and matching PONG; backend decision/evidence committed |
| 3B | Concrete AuthClient and bounded JSON/token parsing committed in 8eb78fe; synthetic host/UBSan and PS5 compile checks pass | Real QR encoding/presentation, service lifecycle integration and direct PS5 approval/restore/sign-out |
| 3C | Authenticated GetOwnedGames adapter and bounded worker implemented; synthetic tests and PS5 cross-compile pass | Connect real records/cache to selected consumer, retrieve actual account data on PS5; authenticated CM remains unproven |
| 4A.1 | Live account controller, private storage and real QR rendering implemented; host verified | Direct PS5 approval/restore/sign-out still pending; production preferences remain 4B |
| 4A.2 | Production library/controller/cache binding implemented; offline/retry and deletion ordering host-tested | Persistent annotations and coordinated real-account PS5 acceptance |
| 4A.3 | Nine static fixture textures; font atlases subset fixture text | Real async fetch/decode, bounded texture lifecycle, stale-result rejection, dynamic title coverage |
| 4B | Merged controller/search/settings behavior has host simulation evidence | Production queries/preferences, connected journey, physical pad/native functional checks; performance acceptance deferred |

Selected UI baseline: `src/homebrew/`, `cmake/HomebrewUI.cmake`, and `cmake/HomebrewPS5.cmake`. The renderer reads state/view projections from `Simulation`; extraction of a production controller must preserve those screens and navigation rather than create a second presentation. `src/library/model.h` and `cache.*` remain renderer-independent real-data components. Reusable selected-renderer fixes belong in ps5-homebrew-ui, with the checked-in dependency patches retained until upstream integration.

Evidence at source `3796598`, documented in `703dd1f`: selected host CTest **11/11**, existing suite **14/14**, eight **host-rendered** native-resolution 1080p/4K captures, 128 deterministic focus-animation frames and a locally built PS5 package. See [post-selection evidence](../../results/ui-comparison/homebrew-ui/fixes-2026-10-04/README.md). No selected-UI console deployment, physical pad acceptance, real authentication/library or performance claim follows from this evidence.

The comparison profile, QR, installs, downloads and runtime/session statuses are simulated. Normal production builds must not restore synthetic accounts or expose fixture completion controls. Keep the comparison fixtures immutable and simulation mode explicitly labelled. The six old RmlUi WIP files are preserved in stash `1f8f649d6e1430f1898b942845db53db668f17a4` (see post-selection evidence); do not restore them into the selected UI.

The existing numerical performance proposal is retained as the acceptance target: p99 <=18ms, missed presentations <=0.1%, no burst of three misses, no persistent post-warmup memory growth. Missing hardware counters remain missing, never inferred as zero. Native 3840×2160/60Hz is mandatory independently of those measurements.

## Historical implementation evidence

The entries below describe the original RmlFocus consumer and remain useful regression/provenance records. They do not establish acceptance for the selected Hui consumer.

Independent progress: deterministic Unicode query implementation uses vendored, revision/hash-pinned utf8proc 2.11.3 (upstream MIT/Unicode licenses preserved). A failing empty-query test preceded implementation; host tests now include extended Latin, multi-character folding, exact non-Latin substring, punctuation, stable sorting, metadata filter, no-results recovery, and malformed text.

Host scene verification: both normal and fixture suites pass 8/8. Screenshot mode disables host swap synchronization because unattended macOS presentation blocked at the first swap; checked-in 3840×2160 captures verify layout only, never timing. PS5 fixture packaging passes; representative console performance remains open.

Controller keyboard lives in RmlFocus main `31d2d8fd39ca3e3bb3ba91a43f50561c04429d57` after independent review and 23 passing framework tests. Launcher normal/fixture host suites pass 9/9 with controller search, confirm/cancel, retained query and empty-results recovery. Graphical captures under results/20261004-search-host are layout evidence only; the PS5 graphical package builds but this search revision is not console accepted.

Native visual evidence: results/20261004-library-ps5 records a direct 3840×2160 framebuffer capture, search text entry/cancel, and restored-focus details navigation. Host suite now passes 10/10. Native capture runs are excluded from sustained performance acceptance, and observed frame intervals exceed the target. No 4K60 claim is made.

User direction (2026-10-04): defer 4K performance work for now. Stop profiling, MSAA comparisons, and RSS/soak work; preserve existing diagnostics and evidence. Continue Steam networking and the connected-library implementation. Native 4K60 remains an unverified final acceptance requirement and is not waived.

Earlier user direction deferred UI to a separate session. That session merged the selected direct-Hui work; the user has now authorized resuming integration here. The old RmlUi partial edits were stashed by that session, as recorded above.

Native Steam transport gate passed: results/20261004-steam-network-ps5 records the packaged title, pinned dependencies/patch, all required transport results and clean exit. Backend selection is recorded in steam-backend-decision.md; authentication and owned-library retrieval remain incomplete.

Authentication protocol progress: `8eb78fe` implements Begin/Poll/Renew with server-paced, cancelable polling and a 300-second lifetime. Independent review found and resolved fresh-login rejection incorrectly revoking an existing session, plus invalid/empty account-name handling. The regression uses concrete AuthClient through SteamService and proves prior credentials survive a failed replacement. UI connection and live Steam acceptance remain open.

Selected-consumer build progress: common domain/backend targets now exist before renderer selection, with separate pinned curl graphs for selected and legacy consumers. Exact patched-file hashes and source/header/archive/CA watchers reject incremental dependency mutations; the PS5 entrypoint refreshes generated font atlases before packaging. Selected host tests pass 21/21, legacy tests 18/18, and the new actual graphical Home capture matches the committed selected baseline byte-for-byte. Local PS5 package eboot SHA-256: `3a08ac3ac459eb7d1544061240dbd112aa7bfe72678e5bcb18d1734b37967241`. No upload, live sign-in, console acceptance or performance claim was made.

## Live account integration (4A.1)

Normal host and PS5 startup now uses the real AuthClient through a bounded controller/service event bridge. Live mode loads no fixture account, games, installs or runtime readiness. Comparison scenarios and replay remain explicit simulation; normal mode rejects capture/record/snapshot options that could expose live QR challenges. PS5 simulation is a separate build option, and production packages omit catalog/scenario fixtures.

Session storage is checked on the service worker and saved privately through SessionStore, separately from simulation profiles. Sign-out only clears the account presentation after durable deletion succeeds. Failed deletion retains identity and reports failure. Shutdown preserves queued deletion without starting another QR request; request/generation checks discard stale events. Library refresh currently reports unavailable: owned-library integration is still pending, not a successful empty result.

The pinned Nayuki encoder renders the actual challenge in the selected Hui screen. Synthetic challenges decode independently through CoreImage from 1920×1080 and 3840×2160 graphical captures. Existing simulated QR captures remain byte-identical. These are functional host checks, not performance or console acceptance.

Verification: selected host build and CTest **27/27** passed, including live model, controller cancellation/restore/sign-out, private repository, redaction, QR codec and graphical checks. Local PS5 compile/link/sign/package succeeded after the review fixes; normal package contains no fixture directory. Final eboot SHA-256: `07b10b975e16782e5a0b4e8035692a60484a43da815b5504a667c170db6f5623`. Legacy regression tests also passed **18/18**. No console deployment or phone approval was performed for this slice. Remaining gates: PS5-issued QR approval, valid restart restoration, cancellation, sign-out, physical controller behavior, real library/cache/artwork/preferences, and deferred 4K performance.

Next retrieval seam confirmed against unchanged reference `de952b3`: `Packages/SteamKit/Sources/SteamCore/Library.swift` calls authenticated `IPlayerService/GetOwnedGames` with the access token, Steam ID, app info and played-free-game flags. It rejects missing/incomplete games instead of overwriting the cache. This path needs neither a public profile nor a user-supplied API key; authenticated CM acceptance remains a separate backend check.

## Owned-library backend (3C, integration in progress)

Added the pinned reference's authenticated `IPlayerService/GetOwnedGames` request using the stored access token, without a public-profile requirement or user API key. The worker reads session material privately, validates the expected account, cancels replaced requests, and publishes bounded results tagged with request and monotonic account generation. Failure events never masquerade as a successful empty library; a genuine explicit empty result remains supported.

The HTTP adapter bounds response bytes, records, JSON structure and metadata. It validates duplicate entries before keeping the first stable app ID, marks incomplete metadata, retains Unicode titles, maps validated artwork hashes, and preserves known playtime/last-played values without inventing absent facts. Committed synthetic response fixtures cover populated, empty and incomplete libraries. Cache schema 2 preserves optional history and continues reading schema 1 as history unknown; lossy schema-1 writes are rejected.

Verification: focused library HTTP/service/cache tests **3/3** pass, including cancellation, stale network/storage completions, account mismatch, generation rollback, metadata boundaries, and old/new cache formats. The library adapter, worker and cache compile with the Prospero toolchain. This is backend evidence only: production presentation wiring, offline recovery, artwork fetching and real-account console retrieval remain pending.

## Selected library projection (4A.2 boundary)

The selected model now accepts typed library deliveries transactionally. Loading/failure preserves valid records; successful empty snapshots clear them; stable IDs preserve focus/scroll and choose a deterministic neighbor on removal. Real playtime and last-played values are projected; unsupported installation/runtime actions remain unavailable. Library loading does not change authentication state. Production wall time is injected independently of render animation time.

Independent review and actual graphical captures exposed and resolved placeholder title overflow, fixture art/download badge leaks, omitted unavailable-action text, inferred installation status, and Details focus on an invisible disabled row. The real renderer now uses safe placeholders, explicit unknown facts and enabled personal actions. Selected host **31/31**, graphical Home/Library/Details inspection and a local PS5 package passed. See [synthetic live projection evidence](../../results/20261004-live-library-projection/README.md).

This prepares the existing presentation; normal startup still needs the production LibraryController binding to the new worker and cache. Real Steam/CM acceptance, offline recovery, persisted account annotations/preferences, asynchronous artwork, dynamic title coverage and physical controller checks remain open. Performance work remains deferred.


## Connected library and offline startup (4A.2 integration)

Normal Mac/PS5 startup now constructs the real owned-library client, worker, account-scoped cache and presentation controller. Authentication success starts refresh; a valid saved session whose renewal fails specifically for network reasons enables cache-only startup. Rejected credentials never enter offline authentication. Cache IO runs on the worker, cached data appears before refresh, and failed refresh preserves existing records. Schema 3 retains partial-metadata status while reading previous cache versions.

Sign-out waits for cache deletion before deleting credentials. Failed deletion preserves the account and records for retry; accepted deletion survives invalidation and shutdown, including a late save. Request/account generations reject stale completions. Independent review identified a rejected cached projection swallowing its later valid network result; the failing regression now passes with the fresh request retained.

See [service-driven graphical evidence](../../results/20261004-connected-library-host/README.md). Selected host tests pass **32/32**, including service-driven rendering of Home/Library/Details and offline cached records with synthetic data. Local PS5 compile/link/sign/package passes. These checks do not establish real Steam account retrieval or console acceptance. Production annotations/preferences, asynchronous artwork, broader font coverage, authenticated CM and physical controller/native lifecycle checks remain pending. Performance work remains deferred.

Final package eboot SHA-256 for this slice: `5ac83aed391482db01cf239d6d8f8fc2413c0e1a34ddd2d873dcc094b968011c`. Review also resolved repeated destructive actions changing account-switch intent and crash-left temporary cache files surviving deletion.

## Live query and persistence backend (4B in progress)

Live browsing now uses the existing pinned Unicode query folding, with title keys derived once per library delivery. Name ties use stable app IDs; simulation ordering remains unchanged. Live filter choices and the rail omit installation/acquisition/genre/controller facts that the current retrieval path does not supply. Personal compatibility remains filterable. The actual graphical test covers accented search, cancel preserving the query, and recovery from no results. See [live query captures](../../results/20261004-live-query-host/README.md).

The renderer-independent organization store now owns account-scoped favorites, hidden games, collections, personal ratings/notes, query/scope/sort/filter and selected identity. It bounds and validates the versioned file, embeds and checks the account ID, performs private atomic replacement, and removes crash-left temporary files on clear. Existing global reduced-motion preferences are now a shared backend target.

A bounded persistence worker separates global preferences from account organization, coalesces pending saves by revision, rejects older revisions, preserves accepted destructive cleanup, and drains accepted saves at shutdown. Rejected submissions leave accepted work unchanged; stale account events cannot cross generations. Independent review covers this backend before production binding.

**The selected live model is not yet connected to this persistence worker.** Startup/load-versus-edit recovery, latest-input-frame flush, preference/annotation failure notices and retry, and organization deletion before session clear remain the next dependent integration step. No production preference restart acceptance is claimed by the backend tests. Artwork, general title glyph coverage, authenticated CM and coordinated real Steam/console acceptance also remain open. Performance work remains deferred.

Verification for this slice: selected host **35/35**, legacy regression **21/21**, graphical query inspection, local PS5 compile/link/sign/package and explicit Prospero persistence-service target compilation passed. Independent review resolved reserved collection IDs/names and title-key recomputation, and found no remaining blocker. Package eboot SHA-256: `217911e2e80aebc3345d5f1178fdb16d49f677529aefb097f9fd68461b28ff16`. No console deployment, account approval or performance test was performed.
