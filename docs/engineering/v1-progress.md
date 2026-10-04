# V1 execution ledger

Objective: implement the complete connected-library v1 from the approved Foundation to Connected Library plan, including console performance and real Steam acceptance. User expanded scope after milestone 1 and directed work and incremental commits on `main`.

Previous goal turn classification: progress — Unicode query and account-scoped cache were independently reviewed, tested and committed (`838f399`, `39bd670`). Checker fixes and regression tests are committed in `a0c6ac1`. Work continues on main; pending UI/probe/framework changes are not acceptance evidence.

| Task | State | Required evidence |
| --- | --- | --- |
| 1A–1B | Complete | Committed shell evidence under results/20261004T132145Z-shell |
| 2A | Complete host scenes/captures; PS5 packaging passes | Bounded 1,000-game views, identity/focus/artwork tests, real captures |
| 2B | Reviewed checker committed; hardware counters still unavailable | 10-minute scenario, 30-minute soak, native 4K60 report, physical controller |
| 3A | Host transport pass; native probe crashes during DNS; debugging | Direct native-title TLS and CM transport, backend decision |
| 3B | Waiting for 3A | Real Steam QR approval, restart, cancellation/sign-out isolation |
| 3C | Waiting for 3A/3B | Real owned library with metadata/recovery |
| 4A | Account-scoped cache committed; integration awaits services | Connected UI and account-scoped offline caches |
| 4B | Unicode query committed; framework keyboard underway | Controller entry, complete journey, real-data performance acceptance |

Shared model: `src/library/model.h` defines GameId (Steam app ID) and GameSummary. Synthetic state stays outside the real game record. Lead owns application integration, shared interfaces, builds and console coordination. Workers own UI, networking audit/probe, and checker independently. No production Steam backend is selected before task 3A evidence.

The existing numerical performance proposal is retained as the acceptance target: p99 <=18ms, missed presentations <=0.1%, no burst of three misses, no persistent post-warmup memory growth. Missing hardware counters remain missing, never inferred as zero. Native 3840×2160/60Hz is mandatory independently of those measurements.

Ruling: retain the approved plan's RmlUi binding and the accepted milestone 1 graphical consumer while measuring representative scenes. RmlFocus main also provides its newer default Hui binding, whose handoff explicitly retains RmlUi and lists CJK/text-entry gaps. A renderer migration is not assumed; any switch must be justified by measured workload or missing capability rather than restarting the accepted shell.

Independent progress: deterministic Unicode query implementation uses vendored, revision/hash-pinned utf8proc 2.11.3 (upstream MIT/Unicode licenses preserved). A failing empty-query test preceded implementation; host tests now include extended Latin, multi-character folding, exact non-Latin substring, punctuation, stable sorting, metadata filter, no-results recovery, and malformed text.

Host scene verification: both normal and fixture suites pass 8/8. Screenshot mode disables host swap synchronization because unattended macOS presentation blocked at the first swap; checked-in 3840×2160 captures verify layout only, never timing. PS5 fixture packaging passes; representative console performance remains open.
