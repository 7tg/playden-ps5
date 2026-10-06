# Half-Life 2 runtime launch baseline

Tested 2026-10-05 through the PS5 MCP. Half-Life 2 did not reach its game code. Playden's title handoff crashed; a separate direct prospero-win attempt reached Wine and stopped at the missing prefix. No launcher or Wine source was changed for this test.

## Test setup

Half-Life 2 (Steam app 220) remains installed at `/data/homebrew/FAKE77031/games/220`: 1,471 files and 5,770,568,474 bytes, with depots 233, 234, 221 and 222. The downloaded `hl2.exe` is a 127,072-byte PE32 image (machine 0x014c).

Compiled a temporary host driver against this project's `src/runtime/prospero_profile.cpp`, `src/steam/backend/app_info.cpp` and `src/steam/backend/vdf.cpp`. It used `ReadPeArchitecture`, `DraftProsperoProfile` and `PublishProsperoProfile` to produce the attached profile and catalog. Uploaded those to `/data/prospero-win/profiles/`. The profile uses the original installed executable through Z:, `-game hl2 -steam`, `prefix = playden` and `graphics = auto`. No game binaries were replaced, prefix fabricated or redistributables installed.

The installed prospero-win build identifies itself as `b4fc5e5b5c8d9ed4bd5091668c0fbcea0a3e3586-dirty`.

## Playden handoff attempt

Uploaded `PPSA99995 profile=playden-steam-220` to `/data/homebrew/FAKE77031/dev/launch-request.txt`, closed prospero-win, and launched Playden (`FAKE77031`) with the MCP. The request was consumed. `playden-launch.log` records `launch_request title=PPSA99995 args=1 user_result=0`, but no `launch_result`.

The console showed CE-108255-1 for Playden, with its debug overlay reporting that FAKE77031 crashed before KStuff was paused. No new prospero-win session was produced by the handoff. The last completed operation was logging immediately before `sceLncUtilLaunchApp`; the exact crash cause is unconfirmed. Playden's probe omits `sceLncUtilInitialize`, which the local CheatRunner and Itemzflow daemon references call before launch. That is a candidate to investigate, not a verified fix.

## Direct prospero-win control attempt

Dismissed the crash dialog, launched `PPSA99995` with the MCP and pressed Cross on the Half-Life 2 profile. The profile parsed successfully (`session-2.log`) and prospero-win restarted into `mode=game profile=playden-steam-220` (`session-3.log`).

The data helper completed (`helper_completed=1 helper_errno=0 data_after=1`), so the earlier startup blocker is resolved in this installed build. Wine loaded `ntdll.prx` successfully, established display/audio/input sinks, mapped the working directory to `/data/homebrew/FAKE77031/games/220` with `status=0`, and entered `__wine_main`.

Wine then stopped with:

```
WINE wine: chdir to /data/prospero-win/prefixes/playden: No such file or directory
```

It returned to the prospero-win library (`session-4.log`). No prefixes existed under `/data/prospero-win` before the test; no prepared compatible prefix was found in the inspected local runtime checkout and relevant temporary directories. The ready badge is profile availability, not evidence of a runnable game.

## Next baseline steps

1. Provision a PC-initialized prefix compatible with the pinned prospero-win Wine build, including the WoW64 registry setting, `wowprospero.dll`, Wine DLLs, drive mappings and game dependencies. The runtime's `tools/pw_prefix.py` implements its console conversions.
2. Retest the unmodified Steam game files and record the next result. Steam client requirements and graphics selection remain untested; `graphics = auto` displayed GDI on the profile card, whereas the runtime compatibility record uses DXVK for Half-Life 2.
3. Diagnose Playden's title-launch crash and verify argument delivery from FAKE77031 to PPSA99995 independently.
4. Only after game startup, verify menu rendering, loading a level, controller input, quitting and return behavior. No gameplay or performance acceptance was established here.

The uploaded profile/catalog remain on the console for the next attempt. The one-shot Playden request was consumed; no pending automatic launch remains.
