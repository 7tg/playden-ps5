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

## Step 2a — the native call works while Playden runs (option C, chosen) — built 2026-10-07

No resident program. As built (`RuntimeHost::Remove`, `src/homebrew/runtime_host.cpp`):

- [x] Refuse while the game's title runs or its state is unknown, while its launch is undecided (runtime host), and
  while Cloud moves its saves (controller). Preparation is refused while a removal is unfinished.
- [x] Journal in the install record: state `removing`, operation `remove` or `remove-saves` (the saves choice, kept for
  every later attempt), steps `retire` → `unregister` → `bottle` → `erase`. A failure keeps the step and its message;
  startup only reports it; asking again continues from the step.
- [x] `retire`: `manual/remove` (best effort), then one `rename` of `/data/homebrew/<T>` into
  `<state>/trash/<T>-<operation>`; the trash is deleted after `erase` and swept at startup.
- [x] `unregister`: only when the console still lists the title (app.db via `Ps5NativeCoordinator::Inventory`, or
  `/user/app/<T>`). Playden writes `request` (`title=`, `nonce=` the operation ID) into `/data/playden/title-remover`
  (0755, Playden's), streams `title-remover.elf` from its own title folder to the payload loader on `127.0.0.1:9021`
  (elfldr cannot pass arguments over a raw stream, hence the request file), and reads `result` (`nonce=`, `code=`).
  It then polls the inventory (30 × 1 s). Silence is reported as "not confirmed yet" and the next attempt checks the
  inventory before sending again; a non-zero code is reported as the console's refusal.
- [x] `erase`: `EraseAfterUnregister` with a fresh observation (app.db and `/user/app` free, title folder absent).
  ShadowMount's cache is not waited for: it does not refresh while any title runs. The title ID is then free.
- [x] Bottle (`bottle` step): kept by default, see step 3. "Also delete saves and settings" deletes it and any kept copy.
  Deletion never follows links (Wine's `z:` points at `/`). A generated recipe is deleted with the record and made again
  from Steam on the next prepare.

Renames and icon changes can reuse the same path (unregister, then SMP registers the folder again with its new name).

## Step 2b — not needed: fallback if the console ever refuses while an app runs (option B)

A small resident queue helper next to SMP (loaded at boot like kstuff): it accepts register / unregister /
re-register jobs from Playden through a private file queue and runs each when no title runs. SMP keeps mounting games.
Separate design note and review before building, since it is a resident root process.

Option A (our own replacement for SMP, own scan root and launch-time mounting) stays a later option; B's queue and
records carry over to it.

## Step 3 — product work — built 2026-10-07

Changed with the user on 2026-10-07: uninstalling a game **always** removes its prepared PS5 title (a title whose game
files are gone is only a broken Home tile; re-preparing happens on its own after a reinstall). Added scope: kept bottles,
their listing and deletion, and Cloud sync before uninstall.

- [x] Order (`UninstallFlow`): Steam Cloud sync → PS5 title removal (journaled) → Steam download. A Cloud conflict or a
  failed sync stops and asks; uninstalling again after a failed sync goes on without syncing. A failed title removal
  stops, the game stays installed, and the PS5 title row offers Retry (continues the removal, then the download). A game
  without a PS5 title uninstalls as before.
- [x] Dialog: the only choice is "Also delete saves and settings" (off). Without Steam Cloud it says the saves exist only
  on this console; otherwise that Cloud syncs first.
- [x] Kept bottle: renamed to `/data/playden/games/<install>.kept` with `playden-kept.json` (install ID, app ID, game name,
  kept-at time, size). Playden lists kept bottles by scanning the games folder for marked bottles with no install
  record; there is no other database.
- [x] Install brings the saves back: preparing the game again builds a fresh bottle from the current kit and moves the
  kept `drive_c/users` tree (Windows saves and per-user settings files) into it, then deletes the kept copy. Settings the
  game stored in the Wine registry are not carried over (the kit's registry may differ). A fresh bottle is used because
  a new settings publication requires the old `library/profiles` to be absent.
- [x] Details of an uninstalled game with kept saves: "Saves from a previous install (<size>)" and a confirmed "Delete
  saves". Settings > Library: "Kept game data" (game, size, date) with a confirmed delete per entry.
- [x] Host tests: `homebrew_runtime_host` (refusal while running, silent remover, restart and resume, failure at the
  bottle step, inventory-first resend rule, kept marker round trip, restore on re-prepare, deletion without following
  links, kept deletion), `title_remover` (request, answer, stale answer, loader failure, linked directory),
  `homebrew_uninstall_flow` (sync gating, conflict, failure then continue, running title, removal failure),
  `homebrew_library_model` (dialog, Retry, kept rows, Settings list).

## Step 4 — cleanup and acceptance

- [x] Remove the old test titles `FAKE99151–99166` (probe step) and the stale `/data/homebrew/FAKE99xxx` probe folders
  (no `param.json`, unregistered).
- [x] Remove the orphaned bottles `acceptance-hl2-fresh-v16`, its `.ftp-staging` copy and `probe-session-*` through Playden
  (they are private to its user). Keep `steam-220`.
- [x] Done on 2026-10-07 ([evidence](../../../results/20261007-native-uninstall/README.md)).
- Console acceptance (not run yet): deploy the build; prepare a disposable game (for example BALL x PIT through the
  generic path); uninstall it keeping saves and check the Home tile disappears while Playden stays open, the download is
  gone, the kept bottle shows in Details and Settings; reinstall and check the saves come back; then uninstall with
  "Also delete saves and settings" and check nothing is left. Half-Life 2 only with the user's explicit yes.
