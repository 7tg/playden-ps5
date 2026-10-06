# Half-Life 2 played from Playden's Play button — 2026-10-06

Console: FW 13.60, jailbreak payloads plus kstuff. Playden build `553c0bd` plus the pad-user log line (eboot `65a3ff4…`
family; package backups under `build/console-backups/20261006-runtime-host-18`). Kit `hl2-fw1360-dev1` (prospero-win
`1a8a0d5`), title `FAKE78002`.

## Result

1. Details showed Play for the Ready title ([details](details-play.jpg)). Play ran the worker preflight (every check `1`,
   activity stopped), prepared session `44f28d44-c20a-4ca2-9998-f69ea6d0b2e2` and handed it off on the application thread
   (`runtime.log`). The native call returned `0x80940010` at the system-service stage, as recorded before; the title
   started anyway.
2. HL2 reached its menu. Load Game listed the earlier saves; the newest loaded at the locker
   ([loaded](save-loaded.jpg)). Look and movement responded ([looked around](looked-around.jpg)).
3. Pause menu, Quit, Save: the console returned to Home with no error ([Home](quit-to-home.jpg)). The new save
   `hl2/save/half-life-003.sav` was written `0600` in the recorded game root.
4. After ShadowMount released `FAKE78002`, Playden reopened normally: `display open` and `first frame presented` in
   `launch.log`. The splash hang seen earlier did not recur; it was not reproduced, and the console was later found to be
   missing kstuff after its restart, which also stops titles from starting.
5. On startup Playden recovered the pending Play session, read the wrapper's exit record and ended it:
   `play finished ... state=5 code= exit=unknown cleared=1`. Details offered Play again
   ([details](details-after-session.jpg)).
6. The automatic post-session Cloud sync ran after the runtime reported ready (12:33:08) and stopped at a conflict:
   "Saves here and in Steam Cloud both changed" ([toast](reopened-cloud-choice.jpg), `cloud.log`). Which copy to keep is
   left to the player.

## Cloud check before Play (`916cc65`, same day)

Play now syncs Cloud saves first. With HL2's conflict still open, Play showed "Checking saves…" and was disabled
([checking](play-checking-saves.jpg)), then stopped at the conflict with Cloud saves offering "Choose"
([stopped](play-stopped-at-conflict.jpg)). `runtime.log` shows no Play request: the game was not started.

## Not covered

- Uploading the new saves: the conflict choice was not made (left to the player).
- Play starting after a clean check (needs the conflict resolved first).
- A session ended from the system menu instead of the game's Quit (handled by the stopped-title rule, host-tested only).
