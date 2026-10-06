# Ready to play and UI polish on the console — 2026-10-07

Build: `main` at `bdda869`, then `86502fa` and `2afb82f` (descriptions, prompt order, Details layout), FW 13.60. Target: "140"
(Steam app, 87 MB), already downloaded on the console and never prepared. Half-Life 2 (FAKE78002) was not touched, and
no game was uninstalled.

## Verified

1. **Details focus and sizes.** Details of "140" opened with focus on Play; Play, Cloud saves, PS5 title, Favorite and
   More are one height.
2. **Plain words and progress.** Play prepared the game first: the PS5 title line read "Preparing · Setting up the game ·
   36%", then "Copying the Windows environment · 44%", with the fraction on Play; registration read "Waiting for the
   PS5", with no percentage.
3. **Preparation ends waiting for the PS5.** With Playden open, ShadowMountPlus did not register the title, and
   preparation ended at once (no minute of polling): Play read "Ready to play", the PS5 title row "Check again", and the
   line "Waiting for the PS5, which adds the game to Home when Playden quits".
4. **Play quits to Home.** Play quit Playden without a dialog; the "140 (Steam)" tile appeared on the Home screen at
   once (`FAKE78001`, with the game's icon).
5. **Start from Home.** The title started and Wine ran the game, which stopped at "Failed to initialize player: Failed
   to initialize Direct3D" (Unity, generated recipe, graphics "auto"): a compatibility problem outside these plans.
6. **Startup check.** Reopened, Playden checked the waiting title on its own: "Preparing · Waiting for the PS5", then
   "Ready · FAKE78001", without pressing Check again; nothing was registered again.

## Found and fixed during the run

- The "is ready to play" prompt did not appear: the controller announced the game before the model had its new state.
  Fixed in `86502fa` (announce after the projection); not yet seen on the console.
- Every game showed "Description unavailable": the port never asked the Steam store. `86502fa` adds the store
  `appdetails` fetch (description, genres, controller support), seen working on the console for "140".
- The user found Play dimmed with a thin bar unattractive; Play now fills with the accent as the work advances
  (`828c7e4`), not yet seen on the console.
- Genre tags overlapped the footer hints once a description showed; live Details now shows two description lines a page
  and drops tags without room.

## Not verified (blocked)

At the next launch Steam refused the saved session (`auth.log`: `GenerateAccessTokenForApp eresult=63`) and Playden
returned to "Connect Steam". The remaining checks need a signed-in session and a disposable download:

- the ready-to-play prompt after an automatic preparation, and Play/Later in it;
- the accent-fill progress on Play, and deletion progress during an uninstall;
- the kept game data manager, the kept-saves line in Details, the dialog label, and Settings focus after a deletion.
