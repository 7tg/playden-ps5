# Ready to play: register new titles by quitting to Home — plan

Agreed with the user on 2026-10-07. Not implemented yet.

## Problem

ShadowMountPlus (SMP) registers a prepared game's title only when it scans, and it postpones every scan while any title
runs, Playden included ([console run](../../../results/20261007-game-removal/README.md)). A freshly prepared game
therefore stays "Registration pending" while Playden is open. The moment Playden exits, SMP runs its pending targeted
scan of `/data/homebrew` and registers the title within seconds (seen in SMP's log on 2026-10-07).

## Decision

Let SMP register the title when the player leaves Playden. No helper, no timeouts, no native registration.

- Preparing never quits Playden on its own, whether it was started by Play, by the PS5 title row or automatically after a
  download. The player may be browsing.
- When preparation reaches "staged and waiting for the console" (`await-native` after a submitted registration), the game
  counts as **ready to play** for the player.
- Playden shows a notification: **"BALL x PIT is ready to play"**, with the action **Play**. The game's Details shows the
  same state on its Play button ("Ready to play" with the same action).
- The Play action quits Playden to the PS5 Home screen (the existing quit path, `quit_requested` → `hui::sys::quit`).
  SMP then registers the title, its tile appears on Home, and the player starts it from there. The quit dialog is not
  shown for this action; the notification and the button already say what happens.
- A Home launch is already handled: the session is reconciled and Cloud saves are synced on Playden's next start.

## Changes

1. **Runtime state.** Treat a record whose registration was submitted and is awaiting the console as "ready to play"
   (new view flag, e.g. `awaiting_registration`), distinct from a failed or uncertain registration. Today Play-prepares-
   first waits up to 60 s in `RunPrepare` for `await-native`; stop waiting there and finish the prepare request as
   "awaiting registration" instead of polling.
2. **Startup check.** On start, records awaiting registration are checked automatically (query only; Playden never
   registers twice). A confirmed title becomes Ready without the player pressing "Check again". "Check again" stays for
   a title that is still not confirmed.
3. **Controller.** Play-prepares-first: when the prepare finishes as awaiting registration, raise the notification with
   the Play-quit action instead of handing off. Automatic preparation after a download raises the same notification.
   Both prepared-and-awaiting games keep their notice until acted on or until the next start (one per game).
4. **UI.** Details: Play reads "Ready to play — Play quits to the PS5 Home screen, where the game appears"; the PS5 title
   row shows "Waiting for the PS5" with no raw step name. Notification text and action as above.
5. **Quit action.** A new action that requests quit without the confirmation dialog, used only by this CTA.
6. **Tests.** Runtime host: prepare ends as awaiting registration without polling; startup check promotes to Ready.
   Controller/model: notification raised once per game for both Play and automatic preparation; Play on an awaiting game
   requests quit; a Ready game still hands off as today.
7. **Console acceptance.** Download a disposable game → automatic preparation → notification → Play quits to Home → tile
   appears → start it from Home → reopen Playden: the record is Ready and the Home session is reconciled.

## Deferred (to-do)

ShadowMountPlus not registering (stale "TitleDir bridge unavailable" hook, SMP not loaded): handled later. Options to
weigh then: detect it (title still absent on the next start), tell the player, offer "Restart ShadowMountPlus" from
inside Playden, or a native registration fallback. Tracked in [STATUS](../../../STATUS.md).
