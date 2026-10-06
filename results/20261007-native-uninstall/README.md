# Native uninstall while Playden runs, and test cleanup — 2026-10-07

Probe for the game-removal plan ([plan](../../docs/superpowers/plans/2026-10-06-game-removal.md), step 1).

## Result: the console uninstalls a title while Playden runs

`probes/title-uninstall` is a one-shot payload launched through the web file manager's `/api/launch-elf` (which streams
the ELF to the payload loader on `127.0.0.1:9021`). It links `libSceIpmi`, `libSceAppInstUtil`, `libSceUserService` and
`libSceSystemService` like the SDK's `install_app` sample, calls `sceAppInstUtilInitialize`, takes ShadowMountPlus's
authid `0x4800000000000006` and calls `sceAppInstUtilAppUnInstall` for each listed title ([log](uninstall.log)).

- With **Playden (FAKE77031) in the foreground**, `FAKE99151` returned `0` and disappeared from the console's app list.
  Playden kept running. ShadowMountPlus's EBUSY is its own policy, not the platform's.
- The remaining old "Playden Native Probe" titles `FAKE99152–99154` and `FAKE99159–99166` were removed the same way.

Dead ends on the way: loading `libSceAppInstUtil.sprx` at run time with `sceKernelLoadStartModule` crashed the payload
(before and after the authid change), and linking only `-lSceAppInstUtil -lSceUserService` stopped it before `main`.

## Cleanup of probe leftovers

`probes/test-cleanup` deletes an allow-listed set of trees, never following symbolic links (the Wine prefixes link `z:`
to `/`) ([log](cleanup.log)). As uid 0 with all capability bits the payload still cannot read another user's private
directory (`EACCES`), but it may change its mode, so each directory is opened up (0777) just before it is emptied.

Removed: 96 unregistered `/data/homebrew/FAKE99xxx` probe folders and the orphaned bottles
`acceptance-hl2-fresh-v16`, its `.ftp-staging` copy and `probe-session-1073/1084/1089-*` (1231 entries, 0 failures).
Kept: `FAKE77031`, `FAKE78002` and its bottle `steam-220`, and every unrelated homebrew title. `/data` was checked
before and after. The probe folder `/data/uninstall-probe` was removed afterwards.
