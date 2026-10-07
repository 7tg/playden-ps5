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
| 140 (242820) | `unity,d3d11,d3d12,d3d9,dxgi,opengl graphics=dxvk`, pe32 | Ready; Steamworks `changed=steam_api.dll`; kept saves restored; prompt; registered | Hung on the system loading spinner (two tries, 60 s each), no session report | The sandbox showed an empty `download0`: the `/data` mount helper never ran because the ELF loader (port 9021) was down. Not a game result yet |

Session evidence (step 0): Play from Playden on Balatro ended `play finished … state=6 code=wrapper-start-failed`, read
from the game's own session record; no `session-owner-invalid`. The Steamworks step at Play was a no-op
(`changed=-`).

Play from Playden on "140" stopped at "Cloud saves need a choice" (left for the player).

## Ten title slots

The kit now has FAKE78001–FAKE78010 (plan step 3). Uploaded 01:05; Playden admitted it, Half-Life 2 and "140" took
Update in about a second each.

## Console problems seen

- The ELF loader (port 9021) stopped between 00:34 (Balatro's `/data` helper ran) and 00:50. Possibly when Balatro's
  removal sent `title-remover.elf` through it at 00:41 (unconfirmed; "140"'s removal at 00:26 did not stop it). Without
  it every per-game title hangs on the loading spinner. Needs the user (WebKitAutoloader, or restart + jailbreak + kstuff).
- Once, Details stayed on "Checking Steam…" for every game until Playden was restarted.

## Open

- Steam details hang above, if it recurs: capture runtime.log and auth.log at that moment.
- Balatro: LÖVE/PhysFS boot failure. BALL x PIT: SteamStub.
- Still to run: Super Meat Boy, Pizza Tower, "140" again (Unity, the earlier "Failed to initialize Direct3D").
