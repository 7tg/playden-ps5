# Playden PS5 Launcher status and roadmap

Updated 2026-10-06. **Half-Life 2 installs, prepares and plays through Playden on the PS5 with its own private Wine bottle
and its own PS5 title** ("Half-Life 2 (Steam)", FAKE78002), with sound, controller, saves and Steam Cloud checks. The
prospero-win integration branch is merged into `main`. What remains is mostly maintenance (removal, update rollback)
and final acceptance; see [Next session](#next-session).

Evidence for the runtime work: [release kit and audio](results/20261006-release-kit/README.md),
[Play from Playden](results/20261006-play-from-playden/README.md), [recovery UI](results/20261006-recovery-ui/README.md),
[title name and icon](results/20261006-title-name-icon/README.md). The detailed integration handoff is
[docs/engineering/2026-10-06-prospero-win-integration-handoff.md](docs/engineering/2026-10-06-prospero-win-integration-handoff.md)
(its "Remaining work" list predates the items marked done below). [HANDOFF.md](HANDOFF.md) is historical (2026-10-04).

## Completed implementation

| Area | Implemented behavior | Evidence and limits |
| --- | --- | --- |
| Pinned dependencies | ps5-native-app-boilerplate (tooling, `libc.prx`) and ps5-homebrew-ui as submodules under `third_party/` | Boilerplate at upstream latest; Hui at upstream latest plus unpushed local `feat/latin-glyphs` commits (see README) |
| Selected presentation and builds | Merged Home, Library, Details, account/settings, controller keyboard, focus/navigation, and management simulations; selected Mac/PS5 builds with pinned dependencies | Actual graphical host captures and local PS5 packages. The legacy RmlFocus presentation was removed on 2026-10-06 |
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
| Per-game runtime (prospero-win) | Pinned `third_party/prospero-win` (`301246d`, per-game wrapper mode); hash-pinned release-clean kit (runtime `e16ec8f3-ps5-console-r1-neutral`, template `hl2-fresh-v16-neutral`, no host paths); private bottle per game under `/data/playden/games/<install>`; own PS5 title from FAKE78000–78999 with the game's name and Steam cover icon, or the generic floppy icon when the cover is missing (optional " (Steam)" suffix) | Console: prepare, register, in-place runtime upgrade ("Update"), ~10 min of HL2 play over two sessions on the current kits, audio port `status=ok` with ~92% audible buffers, saves written, clean quit to Home |
| Play and sessions | Details Play: worker preflight, main-thread handoff with a session ID, durable launch journal, wrapper session records; a title that stops without a record ends after 3 stopped polls; Play refused while a title is running | Console: Play → HL2 → save → quit → Playden reopens and ends the session |
| Cloud around Play | Cloud uses the bottle; sync before Play (conflict stops Play for the player's choice; a failed check lets the player start without syncing); auto sync after a session; no transfer while the title runs | Console: the HL2 conflict stops Play (choice left to the player) |
| Settings publication and recovery | Journaled settings publication with recovery; cleanup of unused settings requests; detect/repair a title that would refuse to start; "Registration pending" instead of resubmitting | Console runs in `results/20261006-*` |

## Next session

### Remaining roadmap (from the prospero-win roadmap and the integration handoff)

Saves/Cloud migration from earlier locations is **not needed**: nothing has been released, so there is no legacy data to migrate.

| Order | Work | Notes |
| --- | --- | --- |
| 1 | 20+ minute HL2 gameplay run on the current build | Only an older build had a 20-minute session. Capture pacing evidence, save, restart and reload. Console time only |
| 2 | Fresh independent review of the whole integration | Earlier reviews covered slices; the final composition has not had one |
| 3 | M4: remove a prepared game | Built 2026-10-07 ([plan](docs/superpowers/plans/2026-10-06-game-removal.md)) and run on the console ([evidence](results/20261007-game-removal/README.md)): uninstall syncs Cloud, removes the PS5 title through the one-shot `title-remover.elf` while Playden runs, then the download; saves are kept as `<install>.kept`, listed in Settings > Library and deletable. Not yet verified on the console: reinstall bringing kept saves back, refusal while the game runs |
| 4 | M4: kit update rollback | In-place upgrade works; a failed update's rollback is not built or tested |
| 5 | Interruption / power-loss qualification | Deferred by the user: cold restart mid-preparation and retry; settings reader lease against the publisher over a full title run |
| 6 | Cloud and storage cases on the console | Account confirmation, USB-download adoption, unavailable media. The HL2 Cloud conflict is the player's choice (do not resolve it for them) |
| 7 | Publish (when the user says so) | See the publish checklist below |
| 8 | Still unported from the macOS app | First-run setup, reset-app-data UI, ambient backdrop, other stores |
| 8a | Other games on the console | Any Steam game is prepared from its Windows launch options (`steam-<app>-auto`). On the console BALL x PIT prepared (pe64, `Balls.exe`) but crashed at start (Unity on wined3d; session marker refused). **New titles cannot register while Playden is open** (ShadowMountPlus postpones scans while any title runs), so Play-prepares-first stops at "Registration pending" until Playden is closed: needs a decision (register natively like the remover, or a queue helper) |
| 9 | Minor | `tools/console_kit_upload.py --replace` names the set-aside kit after the new kit's hash |
| 10 | Ready to play: built, partly console-checked | [Plan](docs/superpowers/plans/2026-10-07-ready-to-play.md), [console run](results/20261007-ready-to-play/README.md): preparation ends waiting for the PS5, Play quits to Home and the tile appears, the startup check makes it Ready. The prompt after an automatic preparation is not seen on the console yet (Steam session refused; needs a QR sign-in) |
| 11 | UI polish: built, partly console-checked | [Plan](docs/superpowers/plans/2026-10-07-ui-polish.md): Details focus, button sizes, step words and copy progress seen on the console; descriptions from the Steam store added. Kept-data manager, dialog label, Settings focus and removal progress wait for a signed-in run |
| 12 | To do: ShadowMountPlus not registering | Stale "TitleDir bridge unavailable" hook or SMP not loaded leaves a prepared game unregistered. Detect it on the next start and tell the player; consider "Restart ShadowMountPlus" inside Playden or a native registration fallback |
| 13 | To do: deployment without FTP | Can/should Playden replace the zftpd-based deployment (`tools/console_title_deploy.py`, `console_kit_upload.py`) with its own native, verified update path (e.g. Playden downloading and verifying a release itself)? Investigate before deciding |

### Repository layout (2026-10-06)

The checkout is `~/Documents/Projetos/Personal/playden/ps5/playden-ps5` (renamed from `playden-ps5-launcher`). It is
self-contained: patched dependencies are submodules on their forks' `feat/playden-ps5` branch, prebuilt toolchain
pieces come from `scripts/fetch-deps.sh` into `deps/`, and nothing in the build points outside the repository. Setup
and the dependency workflow are in [CONTRIBUTING.md](CONTRIBUTING.md); the forks and our commits are listed in
[docs/engineering/dependencies.md](docs/engineering/dependencies.md). A fresh clone (with the forks taken from the local
submodule histories) builds byte-identical PS5 output, including under a symlinked path. Console smoke test of
this build on 2026-10-06: deploy changed only `eboot.bin`, the Lapy helper and their identity files; Playden started,
the `/data` helper completed, kit `caeeae69…` was admitted with HL2 `result=ok`, Play's Cloud check stopped at the
player's conflict as before, and HL2 started from Home to its menu and quit cleanly.

The original checkouts in `~/Documents/Projetos/Personal/GameNative/ps5-homebrew/` (`prospero-win`, `ps5-homebrew-ui`,
`ps5-opengl`, `PS5-Lapy-JB-Daemon`) and `GameNative/playden-ps5/.deps/prospero-foundation` are no longer used by the
build; every branch from them was copied into the submodules. Delete them when you are satisfied.

### Publish checklist (not done; the user decides when)

1. Create the forks under `github.com/leolobato` and push each `feat/playden-ps5` branch (plus any other branches worth
   keeping): `prospero-win`, `ps5-homebrew-ui`, `ps5-opengl`, `PS5-Lapy-JB-Daemon` (a fork already exists from PR #49),
   and `prospero-foundation`. GitHub allows one fork per repository network per account, and `prospero-foundation` is
   in the `ps5-native-app-boilerplate` network, so create it as a repository named `prospero-foundation`.
2. Attach the compatibility kit to a release, with the Wine and DXVK licenses and pointers to the exact sources.
3. Add a `LICENSE` file (sources carry GPL-3.0-or-later SPDX headers; the repository has none).
4. Push `playden-ps5` last, then check a plain `git clone --recursive` builds.
5. Offer our patches upstream: prospero-win (per-game wrapper, runtime fixes), ps5-homebrew-ui (macOS 4.1, CFF SDF,
   latin glyphs), ps5-opengl (`e9c1104`), Lapy (FAKE titles, location-independent builds).

### Console notes

- After a PS5 restart: re-jailbreak (Media > WebKitAutoloader), and make sure kstuff is loaded (without it titles fail with CE-100008-9).
- If FTP (port 2120) stays closed, delete the stale `/data/zftpd/instance.pid` and start zftpd through the web file manager API on :8888 (`/api/delete`, `/api/launch-elf`).
- Only whoever launched an app (DualSense or MCP) controls it. Claim the console before use and release it when idle. Never sign out.
- Tools: `tools/console_kit_upload.py build/kits/hl2-fw1360-dev1 --replace`, `tools/console_title_deploy.py deploy build/homebrew-ps5/dist/FAKE77031 --backup <dir>`. The bottle folders are private; Playden copies the newest session report to `/data/homebrew/FAKE77031/logs/title-<TITLE>.log` when it starts.

## Working constraints

Work on a feature branch and merge to `main`; commit coherent increments and preserve the selected UI. Never push anywhere until the user says so. Keep reusable selected-renderer fixes in ps5-homebrew-ui and product behavior here.

The launcher title is **FAKE77031**. Coordinate with the runtime effort before using the shared PS5. The PS5 MCP is available: claim the console before use, refresh the claim during long waits, and release it when done. A successful build or upload is never console acceptance.
