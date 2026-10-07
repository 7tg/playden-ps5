# Games without a recipe on the console — 2026-10-07

Build: `feat/new-games` (7331f5e, on `main` d5c3991), FW 13.60, kit `hl2-fw1360-dev1`. Plan:
[more games](../../docs/superpowers/plans/2026-10-07-more-games.md).

## Setup

- "140" (Steam 242820, prepared earlier with the old marker and `graphics = auto`) was uninstalled with the previous
  build, with the user's approval, to free `FAKE78001`. Removal took 12 s; its saves are kept (450 MB).
- Deploy: `console_title_deploy.py` refuses to create new package folders, so `runtime/steamworks` was created over FTP
  first. Backup of the previous build: `build/console-backups/20261007-new-games`.
- After deploy, Half-Life 2 (`FAKE78002`) still passed its startup admission check (its marker is unchanged).

## Results

| Game | Probe | Preparation | Start | Notes |
| --- | --- | --- | --- | --- |
| BALL x PIT (2062430) | `Balls.exe` entry point in `.bind` | Refused before staging: "This game's program has Steam DRM that Playden can't unpack on the PS5 yet." | — | Checked the downloaded `Balls.exe`: sections `.text … .reloc .bind`, entry `0xa9310` in `.bind`. SteamStub likely also caused the 2026-10-06 crash. The download wrote `.playden-steam` (depots, owned DLC) |
| Balatro (2379780) | `unknown graphics=auto` (fixed afterwards: `love.dll` → LÖVE → OpenGL, 96e5b2a) | Ready; Steamworks `found=1 changed=steam_api64.dll`; ready-to-play prompt; Play quit to Home; tile registered | From Home and from Playden's Play: LÖVE stops at boot, `Failed to initialize filesystem: no error` (PhysFS). `userenv.dll` loads and unloads just before | Not graphics: PhysFS init (user profile folder) fails in the bottle. The bottle has `ProfileList\S-1-5-21-0-0-0-1000` → `C:\users\prospero`. Needs a Wine trace (`+userenv,+advapi`) |
| 140 (242820) | `unity,d3d11,d3d12,d3d9,dxgi,opengl graphics=dxvk`, pe32 | Ready; Steamworks `changed=steam_api.dll`; kept saves restored; prompt; registered | 00:5x: hung while the ELF loader was down. 06:52 (loader back): Playden's loading spinner for 3 min, then closed | Unity 5.3.6p5 runs: `output_log.txt` shows the D3D11 device (DXVK), "Steamworks Initialized" through the emulator, 1920x1080, the first scene loaded. Then no present: never `loading done`, `vk_shown` is the spinner's 12/s, CPU almost idle. Re-test with `-force-d3d9` blocked: uninstall stopped at "Cloud saves need a choice" (the player's) |
| Super Meat Boy (40800) | `d3d9 graphics=dxvk`, pe32 | Ready; Steamworks `changed=steam_api.dll`; prompt; registered as FAKE78003 | From Home: Team Meat and Newgrounds intros, main menu, controller works, story cutscene after Start Game | First game started with no recipe at all, on generated defaults (DXVK D3D9, `loading done by=frame`) |
| Pizza Tower (2231450) | `gamemaker,d3d11,dxgi graphics=dxvk`, pe64 | Ready; Steamworks `changed=steam_api64.dll`; registered as FAKE78004 | From Home: back to Home within a second | Wine: `unaligned shared mapping … not supported`, "Could not map … shared section .mydata", c000007b. `.mydata` is writable and shared (0xd0000040) on 4 KB pages; the console's are 16 KB. Now refused before staging (d2acb35). Likely every GameMaker game |
| Hexcells (265890) | `unity-legacy,d3d11,d3d12,d3d9,dxgi,opengl,vulkan graphics=dxvk`, pe32 (after d2acb35) | Ready; Steamworks `changed=Hexcells_Data/Plugins/steam_api.dll`; registered as FAKE78005 | From Home: main menu with `-force-d3d9` | Unity without `UnityPlayer.dll` now starts on D3D9 |

Session evidence (step 0): Play from Playden on Balatro ended `play finished … state=6 code=wrapper-start-failed`, read
from the game's own session record; no `session-owner-invalid`. The Steamworks step at Play was a no-op
(`changed=-`).

Play from Playden on "140" stopped at "Cloud saves need a choice" (left for the player).

## Ten title slots

The kit now has FAKE78001–FAKE78010 (plan step 3). Uploaded 01:05; Playden admitted it, Half-Life 2 and "140" took
Update in about a second each. At 07:23 five game titles were registered at once (FAKE78001–FAKE78005), and Super
Meat Boy and Hexcells started from Home on FAKE78003 and FAKE78005.

Half-Life 2 regression (06:57, refreshed FAKE78002 wrapper, build 231fb4c): from Home to the main menu in under 30 s.

## Console problems seen

- The ELF loader (port 9021) stopped between 00:34 (Balatro's `/data` helper ran) and 00:50. Possibly when Balatro's
  removal sent `title-remover.elf` through it at 00:41 (unconfirmed; "140"'s removal at 00:26 did not stop it). Without
  it every per-game title hangs on the loading spinner. At 06:50 the `elfldr.elf` process (pid 82) was still listed by
  Payload Manager but nothing listened on 9021; loading `playden-publication-elfldr-v026.elf` through Payload Manager
  (`/loadpayload:<path>` on :8084) brought 9021 back without a restart. No removal ran after that.
- The new titles (FAKE78003–FAKE78005) start by id but didn't show in the PS5 Home row by 07:23; Half-Life 2 and "140"
  do.
- The copied session report's `time=` is six hours behind the console clock (00:55 for a 06:52 run).
- Once, Details stayed on "Checking Steam…" for every game until Playden was restarted.

## Open

- Steam details hang above, if it recurs: capture runtime.log and auth.log at that moment.
- Balatro: LÖVE/PhysFS boot failure. BALL x PIT: SteamStub.
- "140" on D3D9: needs the player's Cloud saves choice, then uninstall and install again (the recipe is only
  generated at preparation).
- GameMaker games: Wine could map an unaligned shared section as private (the console can't start a second process to
  share it with); that is a prospero-win change and a new kit.
