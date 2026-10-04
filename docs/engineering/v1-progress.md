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
| 3C | Pending authentication | Authenticated CM, real owned library and metadata, bounded recovery; no public-profile/key requirement |
| 4A.1 | Existing account screens use simulation | Production controller, real service events, private QR lifecycle, separate credentials and account-scoped settings |
| 4A.2 | Account-scoped LibraryCache exists; selected UI not connected | Real records, generation isolation, offline/retry, identity/focus preservation |
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
