# First real Steam installs on the console (2026-10-05)

Title FAKE77031 on the dev PS5 (firmware 12.02), signed into a real Steam account. Installs run through the real depot pipeline (`SteamInstallRunner` → `DepotInstaller`), not the simulator. Driven for unattended runs by `/app0/dev/install-request.txt` (one Steam app ID), which queues `InstallService::Install` once after the first frame.

## app 140 — installed

- Downloaded, decrypted and assembled from Steam's licensed Windows depots to `/app0/games/140`.
- Result: `140.exe`, `140_Data/`, `steam_api.dll`, install record `.playden-install.json`; **58 files, 87 MB**.
- The crash-and-resume path was exercised: after an interrupted run the job came back **Paused** ("Interrupted. Resume to continue.") from the on-disk journal, and Resume completed it without re-fetching finished chunks.

## app 220 (Half-Life 2) — downloading, pause/resume verified

- Selected because prospero-win's compatibility list and Wine's own list treat the Source engine as safe, and it is a large multi-depot title that exercises the pipeline harder than app 140.
- Reached ~1.5 GB / 2.8 GB, then was interrupted (the console was borrowed to test the prospero-win runtime). On relaunch the job reappeared as **Paused**; "Job resume 220" from the Downloads actions continued it from the journal (byte counts repopulate after the manifest/app-info step), and chunk writes advanced past the interruption point.

## Console findings (also in TODO.md)

- **`statfs`/`statvfs` terminate the title.** The pre-install free-space check is skipped on PS5 (`DefaultFreeBytes` returns max under `PLAYDEN_PLATFORM_PS5`). A permitted free-space source is still wanted.
- **`readdir` returned no entries.** The install service keeps a `.playden-games` registry and each install writes a `.playden-files` list used for uninstall; a directory walk is only a fallback.
- **HTTP responses are capped at 16 MiB.** The depot manifest fetch is limited to that cap; a title with a larger manifest needs a streamed or larger-cap fetch.
- Depot key refusals were fixed by selecting only **licensed** depots; the Details size estimate still over-counts because it sums every Windows/English depot rather than the licensed set.

## What this does not establish

No Wine/runtime execution: these are installed Windows game files on the console's storage, not a running game. Launching them is the prospero-win runtime milestone, which is blocked by prospero-win's own `/data` helper on this firmware — see [prospero-win-investigation.md](../../docs/engineering/prospero-win-investigation.md).
