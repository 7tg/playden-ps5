# Removing prepared games (M4) — plan

Agreed with the user on 2026-10-06. Goal: Uninstall in Playden can also remove a game's PS5 title, safely and
recoverably, keeping saves by default; and the old test titles and bottles get cleaned up.

## What we know

- ShadowMountPlus (SMP) registers our folder titles and mounts them when they start. Its `games/uninstall` route refuses
  with EBUSY whenever any PPSA/CUSA/FAKE title runs, Playden included, even when called from another device
  (`sm_api_service.c:1898`, confirmed on the console in `results/20261006-fake78001-cleanup`). The refusal is SMP's own
  pre-check; the native call underneath is `sceAppInstUtilAppUnInstall`.
- SMP has no queued or deferred uninstall. `auto_remove_missing_games` covers PPSA titles only.
- A folder that stays under `/data/homebrew` is registered again on the next scan, so the source folder must go first.
- The Playden title cannot load `libSceAppInstUtil` itself (`0x80020063`, `results/20261005-prospero-contract`): even with
  the Lapy `/data` grant it lacks the authid SMP sets. A payload process (SMP's model: `kernel_set_ucred_authid`) can.

## Step 1 — probe (decides the design)

A one-shot payload (`probes/title-uninstall`), launched through the web file manager's `/api/launch-elf`, reads a list
of title IDs and calls `sceAppInstUtilAppUnInstall` for each **while Playden runs in the foreground**, logging every
result. Targets: the disposable "Playden Native Probe" titles `FAKE99151–99166` (no source folder, unknown to SMP).
Then verify with the console app list and SMP's `/api/v1/games`, and check that nothing is registered again.

**Outcome (2026-10-07, [evidence](../../../results/20261007-native-uninstall/README.md)): it works.** With Playden in
the foreground the payload removed `FAKE99151` (`sceAppInstUtilAppUnInstall` = 0) and Playden kept running, so step 2a
applies. Requirements found: link `-lSceIpmi -lSceAppInstUtil -lSceUserService -lSceSystemService`, call
`sceAppInstUtilInitialize` before changing the authid. An idle registered title (`FAKE78002`) has no mount (only a
running title's folder is nullfs-mounted into `/system_ex/app`), so no unmount step is needed. ShadowMountPlus's cache
does not refresh while a title runs; in-session confirmation uses `app.db`, the source folder and `/user/app/<T>`.

## Step 2a — the native call works while Playden runs (option C, chosen)

No resident program. Removal from Playden:

1. Refuse while the game's title runs, a session is undecided, or Cloud is moving its saves.
2. Journal each step in the install record (`removing` state, step name), so an interrupted removal resumes on start.
3. Retire the source: `manual/remove`, then rename `/data/homebrew/<T>` in one atomic move out of the scan root (to a
   private trash under `/data/playden`), then delete the trash.
4. Unregister: Playden launches the one-shot uninstall payload with the title ID (same mechanism as the probe, via a
   loopback request that Playden can make), and waits for its result file.
5. Confirm: SMP `Query` absent and `/user/app/<T>` absent. Only then free the title ID and drop the record.
6. Bottle: kept by default (marked unused, reused if the game is prepared again); "Also delete saves and settings" is
   a separate, explicit choice.

Renames and icon changes can reuse the same path (unregister, then SMP registers the folder again with its new name).

## Step 2b — not needed: fallback if the console ever refuses while an app runs (option B)

A small resident queue helper next to SMP (loaded at boot like kstuff): it accepts register / unregister /
re-register jobs from Playden through a private file queue and runs each when no title runs. SMP keeps mounting games.
Separate design note and review before building, since it is a resident root process.

Option A (our own replacement for SMP, own scan root and launch-time mounting) stays a later option; B's queue and
records carry over to it.

## Step 3 — product work (either option)

- Details: Uninstall offers "Remove the PS5 title too" (on by default for prepared games) and the separate save choice.
- Status line while pending ("Removing…", "Removal pending: finishes when Playden closes" for option B).
- Startup reconciliation of `removing` records; title IDs stay allocated until removal is confirmed.
- Host tests with the fake registry and a fake uninstall runner: every step interrupted, failed unregister, missing
  volume, running game, save retention.

## Step 4 — cleanup and acceptance

- Remove the old test titles `FAKE99151–99166` (probe step) and the stale `/data/homebrew/FAKE99xxx` probe folders
  (no `param.json`, unregistered).
- Remove the orphaned bottles `acceptance-hl2-fresh-v16`, its `.ftp-staging` copy and `probe-session-*` through Playden
  (they are private to its user). Keep `steam-220`.
- Console acceptance: remove a prepared game, see its title disappear, prepare HL2 again and confirm its saves.
