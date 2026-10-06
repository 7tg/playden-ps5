# Release-clean kit on the console — 2026-10-06

## Runtime rebuild (Codex worker, OrbStack ps5-amd64)

The console runtime's exact recipe was recovered and reproduced byte-for-byte: `ntdll.prx` `62cf477b…` (e16ec8f3 +
a4c3856 display-handoff sink) and `win32u.prx` `8ae8568e…` (patch 0663 from 4234987 + three PSBC objects from e9c1104).
The whole runtime was then rebuilt with neutral path maps. Packager privacy scan over all 1,704 files: 0 hits. 97 files
identical, the 1,607 others explained one by one (PE build/debug metadata, five PRXs equal to a path-only relink of the
console's own inputs, regenerated inventory). Full report: `build/wine-neutral/REPORT.md` (build output, not tracked).

## Kit

The bottle template's 516 path-carrying Wine builtins were replaced by the same modules from the neutral runtime
([substitutions](template-substitutions.json)); the derived template has its own pin `hl2-fresh-v16-neutral`. The kit
packages without `--development-build-paths`, with no host-path exceptions.

## Console (FW 13.60)

- Kit `5807db5f…` installed (3,559 files verified by read-back). Details offered Update; the first attempt was refused
  (the respin check did not allow a new runtime revision, fixed in `c004357`); the second upgraded the title in place:
  `runtime upgrade app=220 recorded=1`, refresh state 0, no new registration. `ntdll.prx`/`win32u.prx` in
  `/data/homebrew/FAKE78002` hashed equal to the neutral build.
- HL2 from Home: menu, Load Game, the 14:32 save loaded ([loaded](save-loaded.jpg)), about 6 minutes of movement and look
  through the station ([hall](station-hall.jpg)), pause menu Quit with Save: `half-life-004.sav` (0600) written, clean
  return to Home showing "Half-Life 2 (Steam)" ([Home](quit-to-home.jpg)). Admission check afterwards: `result=ok`.

## Audio regression found and fixed

The session report showed `audio=0` throughout. Every start since the private umask (`eba69f8`) logged
`PW_WINE64 audio sink=1 port=-1 status=state`; only the first 20-minute session, before it, opened the port (`status=ok`)
and counted ~98% audible buffers. prospero-win `301246d` opens the audio port under umask 0 and restores the private
umask before Wine creates files.

Kit `caeeae69…` (manifest `8f49f733…`) with that wrapper, on the console after a restart (zftpd had refused to start
because of a stale `/data/zftpd/instance.pid` left from before the reboot):

- Upload verified 3,559 files; Details offered Update, which refreshed the title in place (`title refresh app=220
  state=0`, `prepare result app=220 ready`). `/data/homebrew/FAKE78002/eboot.bin` hashed equal to the kit (`36edcbe6…`).
- Play from Playden stopped at the HL2 Cloud conflict, which is left to the player; the title was started from Home.
- [Session report](fake78002-audio-session.log), build `301246d`: `audio sink=1 port=536870912 status=ok`. Over about
  three minutes (menu, Load Game of the 17:23 save, movement through the station) the counters reached `audio=31304
  audible=28815`. Pause menu Quit with Save wrote `half-life-005.sav` and returned to Home cleanly.
