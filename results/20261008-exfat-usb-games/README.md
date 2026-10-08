# Games on an exFAT USB disk — 2026-10-08

Build `feat/permissionless-volumes` (eea0d72), eboot `e9283aa2…`; kit `hl2-fw1360-dev1` manifest `a003dcec…` (kit C's
runtime bea2be5 and template, shell rebuilt from prospero-win 33130a9 by `build/kit-c/scripts/make_kit_unowned.sh`, files
`1237d8e8…`, 3,551 files); FW 13.60.

## Why

The test disk is a macOS-formatted exFAT drive, mounted at `/mnt/usb1`. exFAT keeps no owners or permissions: every
entry reports the mount owner and `0777`, and `chmod` returns success without changing anything (checked with the web
file manager's chmod on a downloaded file). Playden refused to prepare a game there ("The game's folder belongs to
another user"), and the kit's launch admission would have refused the folder too.

## Change

- Playden probes the game folder (create a file, set `0600`, read it back). On a folder that keeps no owners it skips the
  owner check and the `chmod 0755`, publishes settings as metadata schema 3 with `game_root.unowned=1`, and handles
  `dxvk.conf` there without owner or mode checks (`DirectoryPolicy::kUnownedTree`). Prepare refuses such a folder unless
  the kit lists `game-root-unowned`.
- prospero-win 33130a9: launch admission accepts any owner and mode on the game working folder and its `dxvk.conf` when
  the metadata says so; type, symlink, link-count and identity checks and all private-file checks stay.
- Separately, extended storage and USB candidates must be mount points; the console's empty `/mnt/ext0` was offered as
  "Extended storage" before and Playden had created `/mnt/ext0/playden/games` on system storage (removed).

## Console run

| Step | Result |
| --- | --- |
| New kit upload | 3,552 files verified; old kit moved to `.hl2-fw1360-dev1.retired-1237d8e87eb4` (named after the new files hash, as before) |
| Playden start | `kit_admission … manifest=a003dcec… prospero_win=33130a9 ignored_capabilities=-`; all seven prepared titles passed their admission check and now offer Update |
| Downloads screen | Console storage, M.2 SSD, USB drive (`usb0`, a real mount with 512-byte directory blocks), USB drive 2; no Extended storage |
| Settings > Games storage | Set to USB drive 2 through the UI |
| Undertale (391540, GameMaker, D3D9/DXVK, pe32) | Downloaded to `/mnt/usb1/playden/games/391540`; Prepare logged `game root without ownership`, Steamworks changed `steam_api.dll`, publication completed, registration pending until Playden closed (FAKE78008) |
| Start from Home | Intro plays (story panels, "MT. EBOTT 201X"); input not checked |

Super Meat Boy was the first choice (known to play) but its uninstall stopped at "Cloud saves need a choice", which is
left to the player.

## Open

- Prepared games on console storage need Update for the new shell.
- The Prepare refusal with an older kit was only covered by the host test, not on the console.
