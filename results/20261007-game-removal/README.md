# Game removal on the console — 2026-10-07

Build: `feat/game-removal` with the fixes below, FW 13.60. Target: BALL x PIT (Steam app 2062430), a disposable game.
Half-Life 2 (FAKE78002) was not touched. [Crash log of the BALL x PIT title](title-FAKE78001.log).

## What ran

1. **Generic preparation.** Play on the never-prepared game generated `steam-2062430-auto` (`Balls.exe`, pe64), staged
   the title as `FAKE78001` and copied the bottle. Registration stayed pending while Playden was open (see below);
   after closing Playden, ShadowMountPlus first reported "Register failed … TitleDir bridge unavailable" (stale hook);
   re-running the installed ShadowMountPlus payload registered `FAKE78001`, and "Check again" made it Ready.
2. **Play.** Cloud synced, the title started and Wine loaded the Unity player, then the title crashed (CE-108255-1).
   The game runs Direct3D 11 through wined3d (graphics "auto"), and the session record was refused ("managed bottle
   ownership marker is invalid"). Not investigated yet.
3. **Uninstall, keeping saves.** Cloud synced, the manual source was removed and the title folder retired. The remover
   payload was sent but refused the request: it checked the owner of `/data/homebrew/FAKE77031` (uid 0, from the FTP
   deploy) against Playden's uid 1. Fixed: it now checks the owner of Playden's private `runtime-state` folder. After
   redeploying only `title-remover.elf`, Retry unregistered `FAKE78001` (`code=0`, app.db row gone, ShadowMountPlus
   logged the removal) **while Playden stayed in the foreground**.
4. **Crash after unregister.** Playden then crashed, and crashed again at every start, while emptying its trash of the
   retired title folder: `std::filesystem::remove_all` crashes the console title. Pinpointed with startup log lines;
   replaced by an lstat/unlink/rmdir walk that never follows links. Startup then emptied the trash (~9 s, the retired
   folder holds the Wine runtime) and ran normally.
5. **Finish.** Uninstall removed the download; Settings > Library listed "Kept: BALL x PIT (Steam)" (450 MB, 2026-10-06);
   deleting it removed the kept bottle; "Clean up" removed the leftover settings request.

End state: `/data/homebrew` has no `FAKE78001`, `/user/app/FAKE78001` is absent, `/data/playden/games` holds only
`steam-220`.

## Not verified

- Reinstalling a game and getting its kept saves back.
- Refusal of uninstall while the game runs.

## Found

- New titles cannot register while Playden is open: ShadowMountPlus postpones every scan while any title runs, so Play
  on an unprepared game stops at "Registration pending" until Playden is closed.
- UX: Details opened while the runtime checks focuses Cloud saves instead of Play; raw step names (`stage-title`,
  `await-native`) are shown; "Also delete saves and settings" is cut off; the kept name carries " (Steam)"; Details of
  an uninstalled game shows "Delete saves" but not the "Saves from a previous install" line; a closed dialog in
  Settings can leave focus in the section list.
