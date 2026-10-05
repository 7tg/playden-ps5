# dxvk.conf probe (roadmap step S0)

Firmware 13.60, 2026-10-05, 20:57–20:59 local time. Question: does DXVK in the
prospero-win kit read `dxvk.conf` from the game's working directory?

- **Setup.** prospero-win PPSA99995, profile `playden-steam-220` (HL2, `graphics = dxvk`,
  `d3d9=n`, `working_directory = Z:\data\homebrew\FAKE77031\games\220`). The profile leaves
  `show_fps` at its default, so DXVK's FPS counter is drawn top left.
- **Baseline.** Without `dxvk.conf`, the HL2 main menu shows **59.1 FPS**
  (`baseline-59fps.jpg`).
- **Probe.** Uploaded `dxvk.conf` (this folder) to `/data/homebrew/FAKE77031/games/220/`
  through the web file manager; read back, SHA-256 matched. Relaunched HL2. The main menu
  shows **29.9 FPS** (`capped-30fps.jpg`).
- **Cleanup.** Closed HL2 with the console's Close app, deleted `dxvk.conf`, listed the
  folder to confirm it is gone. No other file changed. Claim released.

Result: DXVK 2.6.2 in this kit loads `dxvk.conf` from the game folder (DXVK's
`config.cpp` falls back to `dxvk.conf` in the current directory; prospero-win sets the Wine
working directory from the profile). Frame limit, VSync, frame latency and reported VRAM
can be applied by Playden alone. The existing `hl2.dxvk-cache` in the same folder is
consistent with this.

Not shown: VSync, latency and VRAM options were not measured here; each still needs its
own console acceptance when its row ships (roadmap S5).

## VSync off (21:07–21:09)

- `dxvk-vsync-off.conf` (`dxgi.syncInterval = 0`, `d3d9.presentInterval = 0`) in the same
  folder. The menu read **56.0** then **59.4 FPS** (`vsync-off-56fps.jpg`,
  `vsync-off-59fps.jpg`): no uncapping. The PS5 presentation path still paces at 60 Hz, as
  the settings catalogue predicted (tearing/immediate modes need RADV/display-plane work).
- The file was removed afterwards and the folder listed to confirm. Claim released.

Result: VSync stays disabled in Game settings with the reason "The PS5 display paces frames
at 60 Hz; VSync cannot be turned off yet." Frame latency and reported VRAM were not
observable here (prospero-win session logs do not show DXVK's config lines), so those rows
stay disabled until a check proves an effect.

## Overlay detail through dxvk.hud (21:39–21:41)

- Temporarily added `show_fps = false` to `/data/prospero-win/profiles/playden-steam-220.profile`
  (original SHA-256 `014fc1af…4ff3`) and uploaded `dxvk-hud.conf` (`dxvk.hud = fps,frametimes`)
  as the HL2 `dxvk.conf`.
- The HL2 menu showed the FPS counter and DXVK's frame-time graph (`hud-frametimes.jpg`), so
  `dxvk.hud` drives the overlay when prospero-win does not set `DXVK_HUD`.
- Restored the original profile (read back, same SHA-256) and deleted `dxvk.conf`. Claim released.

Result: decision D1 (a) works. Playden writes the DXVK overlay into dxvk.conf and keeps
`show_fps = false` for DXVK games when the kit reads dxvk.conf.
