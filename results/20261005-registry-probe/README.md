# Registry settings probe (S7, decision D2)

Date: 2026-10-05. Console: firmware 13.60, prospero-win PPSA99995, HL2 (`playden-steam-220`),
bottle `/data/prospero-win/prefixes/playden`. Question: does Wine read the `user.reg` edits
Playden makes for Windows version and text scale?

prospero-win runs Wine on the stored prefix itself (`WINEPREFIX=<root>/prefixes/<name>`,
`native/wine64_main.c`), so an edit to the stopped bottle's `user.reg` is what Wine loads.

Each run: download `user.reg`, apply `ApplyRegistryValues` (Playden's code, host build),
upload and verify by SHA-256, launch HL2 to its menu, close, read `user.reg` back, then
restore the original (SHA-256 `17e38b16…d7e6` before and after both runs).

## Run 1: Windows version XP, text scale 144 in `Software\Wine\Fonts`

Profile temporarily set `winedebug = err+all,+ver` (restored, SHA-256 `014fc1af…4ff3`).

- `run1-ver-trace.log`: `getting version from L"AppDefaults\\hl2.exe"`,
  `got win version L"winxp"`, `got 5.1 ... Service Pack 3`. **Windows version works.**
- `run1-after-launch.txt`: Wine kept the `AppDefaults\hl2.exe` value when it re-saved the
  file, but reset `Software\Wine\Fonts` `LogPixels` to `0x60`. That value is Wine 11's font
  cache marker (`win32u/font.c` writes the DPI it used there at start), not the setting.

## Run 2: text scale 120 in `Control Panel\Desktop`

Wine 11 reads the DPI from `HKCU\Control Panel\Desktop` `LogPixels` (`win32u/font.c`,
`sysparams.c`), the value winecfg's DPI slider writes. Playden now writes it there.

- `run2-after-launch-logpixels.txt`: after the launch Wine wrote `0x78` into its font marker,
  so the session ran at 120 DPI. **Text scale works.**

The HL2 menu looked unchanged at 120 DPI: Source draws its own menus. The setting matters for
games that use Windows dialogs and fonts.
