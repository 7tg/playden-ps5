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
