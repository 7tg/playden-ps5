# macOS feature ports on the PS5 (games storage, notifications, logs, Cloud saves)

Title `FAKE77031` on the dev PS5, signed into the real Steam account, deployed over zftpd and launched with the PS5 MCP. Implements the [macOS feature ports plan](../../docs/superpowers/plans/2026-10-05-macos-feature-ports.md). Screenshots are not committed because they show the account name; this record contains no account identifier, token or endpoint.

Host suite: 62/62 (`ctest --test-dir build/homebrew-host`), PS5 package builds.

## Observed on the console

| Feature | Check | Result |
| --- | --- | --- |
| Games storage | Spike: which drives can the title write? | Only console storage. From the title's sandbox `/mnt/ext1` (M.2), `/mnt/ext0` and the USB mounts are not visible, although FTP (outside the sandbox) sees them. The picker therefore lists one drive here |
| Games storage | Old registry format (`140`, `220` one per line) | **Pass.** Both installs still listed after the new registry code started |
| Games storage | Settings → Games storage | **Pass.** "Console storage" shown as the default; the picker opens and marks it Default · Available |
| Games storage | Downloads storage card | **Pass.** Live figures: 5.5 GB used by games, 0 reserved by the queue, "Free space is not reported on this console" (no permitted `statfs`) |
| Games storage | Details | **Pass.** "Installed · 87 MB · On Console storage" under the actions; Location in the info panel |
| Notifications | Uninstall app 140 | **Pass.** "Game uninstalled · 140" toast; Install offered again |
| Notifications | Reinstall app 140 | Install completed ("Installed · 87 MB"); the 6-second "Download complete" toast was missed by the screenshot timing; the transition is covered by the model test |
| Logs | Settings → Logs → Install log | **Pass.** The real install log: UTC-stamped lines with `app=242820`, opened at the newest lines (46 lines), Refresh and Back |
| Cloud saves | 140 → Cloud saves | **Pass.** Real CM `Cloud.GetAppFileChangelist`: "No Cloud saves yet" (the app declares Steam Cloud) |
| Cloud saves | Half-Life 2 → Cloud saves (first build) | **Crash**, then fixed: a 64 KiB stack buffer overflowed the worker thread once local files were compared. Fixed by heap buffers and chunked hashing (`8b52e9d`) |
| Cloud saves | Half-Life 2 → Cloud saves | **Pass.** "Cloud saves synced · Half-Life 2 · 8 saves downloaded": the account's real HL2 saves (`hl2/save/half-life-000.sav` and its screenshot/state files, plus two bare-name config files) written where the game reads them; baseline recorded |
| Cloud saves | Half-Life 2 → Cloud saves again | **Pass.** "Saves are up to date" |
| Cloud saves | Uploads | **Not run on purpose.** Nothing was uploaded to the real account. The upload batch, conflict choices, account confirmation, stale-change refusal and damaged-download refusal are covered by `tests/steam_cloud_test.cpp` against a fake Steam Cloud |

Also on the console: "Open logs" on a download now shows real lines instead of fixture text; download actions read "Resume download" etc.; the empty Downloads screen no longer mentions a simulated transfer.

## What this does not establish

- Saves syncing around a game launch: there is no launch yet (prospero-win's own `/data` helper blocks it; see [prospero-win-investigation.md](../../docs/engineering/prospero-win-investigation.md)). `CloudService::Sync` is the hook for download-before-launch and upload-after-exit.
- A second drive on the console: the title cannot see one.
- A real upload, a real conflict and the account confirmation against Steam (host-tested only).
