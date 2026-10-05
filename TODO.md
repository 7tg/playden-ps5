# TODO log

Deferred items found during console runs. Newest first. Remove an item when it is fixed.

## Runtime (prospero-win)

- A runtime image deployed to `/data/homebrew/<TITLE>` must have its `eboot.bin` and `sce_module/*.prx` marked executable; web-file-manager uploads land as `rw-` and the console then refuses the title with 0x80aa001a before any code runs. Playden's own deploy already does this — ensure any prospero-win/runtime image Playden ever stages is chmod'd on upload (`/api/chmod` recursive, or the loader's own mode).
- prospero-win's `/data` Lapy helper is firmware-gated (12.02) and did not complete on this console even with elfldr on 9021 (`helper_errno=54`). Blocked on the prospero-win owner; revisit once a compatible helper/loader build exists before wiring the LaunchApp handoff.

## Storage, logs and Cloud saves

- The title's sandbox cannot see `/mnt/ext1` (M.2), `/mnt/ext0` or USB mounts, so the games-storage picker only offers console storage on the PS5 even though the picker and per-drive routing work (covered by host tests; the Mac app's `--extra-volume` adds test drives). Extra drives need a permitted path into the sandbox (the same `/data`-style access prospero-win needs).
- Free space is still not reported on the console (no permitted `statfs`): the storage card shows space used by games and reserved by the queue instead.
- Cloud saves sync on demand from Details. Download-before-launch and upload-after-exit hooks wait for the prospero-win launch handoff; `CloudService::Sync` is the entry point to call there.
- Cloud uploads (the `Cloud.*` upload batch) are verified only against the fake Steam Cloud in `tests/steam_cloud_test.cpp`; on the console only downloads were run, deliberately, so the real account's Cloud saves were not changed. Verify an upload with a throwaway save once a game can run.
- An upload batch interrupted mid-way is not journaled; the next sync re-checks Steam's change number, but the open batch is left for Steam to expire.
- Saves Steam keeps under `%GameInstall%` live in the install folder and go with an uninstall (as on macOS); Windows user-folder saves live under `.playden-saves/<app id>` and survive it.
- Cloud files over 16 MiB (the HTTP response cap) cannot be downloaded yet.

## Downloads

- Details size estimates sum every Windows/English depot (140: 610 MB estimated, 87 MB installed); use the licensed depots, as the installer does.
- Chunks download one at a time over a new TLS connection each; add bounded parallelism and connection reuse.

- Free space is not checked before installing on the console: `statfs`/`statvfs` terminate the title there. Find a permitted way to read free space (e.g. a system service) and restore the pre-check.
- The console's `readdir` returned no entries: the install service keeps a registry and installs write a file list used for removal; a directory walk remains only as a fallback for files the list does not cover.

- Depot manifests larger than 16 MiB (the HTTP client's response cap) cannot be downloaded yet; very large games need a streamed or larger-cap manifest fetch.

## Cosmetic and UX

- Downloads: a resumed job shows "0.0 GB / 0.0 GB" until its manifest step reports the totals.

- **Text is small for a TV.** Fonts across Home, Library, Details, Downloads and Settings read small from couch distance; raise the type scale for 10-foot viewing (user feedback, 2026-10-05).
- Details: after app info arrives, focus stays on Favorite instead of moving to Install.
- Downloads: a failed job shows the stage ("Download") in red but not its failure text; show the job's reason.
- Some covers stay placeholders (for example Proteus): titles without a `library_600x900` portrait on the Steam CDN. Consider a header-image fallback.
