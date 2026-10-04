# V1 execution ledger

Objective: implement the complete connected-library v1 from the approved Foundation to Connected Library plan, including console performance and real Steam acceptance. User expanded scope after milestone 1 and directed work and incremental commits on `main`.

Previous goal turn classification: progress — native 3840×2160 framebuffer evidence, controller-routing checks, and reviewed process-memory diagnostics were committed in `0fc94d9`. The kernel advertises a 1096-byte process record; the pinned SDK declares 1088 bytes, so RSS remains unknown. Work continues on main; uncommitted transport probes are not acceptance evidence.

| Task | State | Required evidence |
| --- | --- | --- |
| 1A–1B | Complete | Committed shell evidence under results/20261004T132145Z-shell |
| 2A | Complete host scenes/captures; native PS5 visual/routing evidence recorded | Bounded 1,000-game views, identity/focus/artwork tests, real captures |
| 2B | Native frame intervals exceed target; RSS and presentation counters unavailable | 10-minute scenario, 30-minute soak, native 4K60 report, physical controller |
| 3A | Native DNS and bounded empty poll pass; curl request stalls before timeout | Direct native-title TLS and CM transport, backend decision |
| 3B | Waiting for 3A | Real Steam QR approval, restart, cancellation/sign-out isolation |
| 3C | Waiting for 3A/3B | Real owned library with metadata/recovery |
| 4A | Account-scoped cache committed; integration awaits services | Connected UI and account-scoped offline caches |
| 4B | Controller search implemented/reviewed; injected native input passes; physical check pending | Controller entry, complete journey, real-data performance acceptance |

Shared model: `src/library/model.h` defines GameId (Steam app ID) and GameSummary. Synthetic state stays outside the real game record. Lead owns application integration, shared interfaces, builds and console coordination. Workers own UI, networking audit/probe, and checker independently. No production Steam backend is selected before task 3A evidence.

The existing numerical performance proposal is retained as the acceptance target: p99 <=18ms, missed presentations <=0.1%, no burst of three misses, no persistent post-warmup memory growth. Missing hardware counters remain missing, never inferred as zero. Native 3840×2160/60Hz is mandatory independently of those measurements.

Ruling: retain the approved plan's RmlUi binding and the accepted milestone 1 graphical consumer while measuring representative scenes. RmlFocus main also provides its newer default Hui binding, whose handoff explicitly retains RmlUi and lists CJK/text-entry gaps. A renderer migration is not assumed; any switch must be justified by measured workload or missing capability rather than restarting the accepted shell.

Independent progress: deterministic Unicode query implementation uses vendored, revision/hash-pinned utf8proc 2.11.3 (upstream MIT/Unicode licenses preserved). A failing empty-query test preceded implementation; host tests now include extended Latin, multi-character folding, exact non-Latin substring, punctuation, stable sorting, metadata filter, no-results recovery, and malformed text.

Host scene verification: both normal and fixture suites pass 8/8. Screenshot mode disables host swap synchronization because unattended macOS presentation blocked at the first swap; checked-in 3840×2160 captures verify layout only, never timing. PS5 fixture packaging passes; representative console performance remains open.

Controller keyboard lives in RmlFocus main `31d2d8fd39ca3e3bb3ba91a43f50561c04429d57` after independent review and 23 passing framework tests. Launcher normal/fixture host suites pass 9/9 with controller search, confirm/cancel, retained query and empty-results recovery. Graphical captures under results/20261004-search-host are layout evidence only; the PS5 graphical package builds but this search revision is not console accepted.

Native visual evidence: results/20261004-library-ps5 records a direct 3840×2160 framebuffer capture, search text entry/cancel, and restored-focus details navigation. Host suite now passes 10/10. Native capture runs are excluded from sustained performance acceptance, and observed frame intervals exceed the target. No 4K60 claim is made.

User direction (2026-10-04): defer 4K performance work for now. Stop profiling, MSAA comparisons, and RSS/soak work; preserve existing diagnostics and evidence. Continue Steam networking and the connected-library implementation. Native 4K60 remains an unverified final acceptance requirement and is not waived.

User direction (2026-10-04): defer all UI work to a separate session/agent. This session continues Steam networking/backend only. Uncommitted partial sort/filter work is preserved in src/ui/shell.{h,cpp}, assets/ui/library.rml, assets/ui/playden.rcss, assets/ui/library-options.rml, and tests/library_options_test.cpp. The final added no-results assertions have not been compiled/run; no UI completion claim or UI commit was made.
