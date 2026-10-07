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

## Second run, signed in again (build `a67ef1a`)

The user signed in again. Playden restored the session on the new build.

- **Descriptions** showed for BALL x PIT, with "Read more" paging and "Controller: Full".
- **Install → automatic preparation** started after the 236 MB download and was refused: "A PS5 title could not be
  reserved: no supported native title variant is available". The installed kit has two title slots (`FAKE78001`,
  `FAKE78002`), held by "140" and Half-Life 2, so a third game cannot be prepared. "140" and Half-Life 2 were not
  removed to free a slot, since the user installed them.
- **Uninstall without a PS5 title** showed the two-button dialog (no saves choice) and removed the download.
- **Settings > Library** showed one "Kept game data · None" row. "Clean up" on "Unused game settings data" (left by the
  refused preparation) removed the row and focus stayed in Library on the nearest row instead of the rail; it landed on a
  row that cannot be chosen, so focus now prefers one that can.
- **Details focus** fell to More when Install became the download; it now follows the primary action.
- **Sign-in:** a refused saved session (`eresult=63`) now starts a new QR code at once (host-tested; not seen on the
  console, the session was valid).

## Third run, by the feat/new-games session (build `7331f5e` on `d5c3991`, ~02:25–02:37)

With the user's approval, "140" was uninstalled to free a title slot.

- **Uninstall with a PS5 title:** the dialog read Cancel / "Delete saves too · Off" / Uninstall. During removal Details
  showed "Play · Removing PS5 title…" and "PS5 title · Removing · Removing the game's PS5 title"; `runtime.log`: retire →
  unregister `code=0` → complete in 12 s.
- **Kept saves in Details:** afterwards "Install", "Delete saves", and the line "Saves from a previous install · 450 MB ·
  Installing the game again brings them back."
- **Ready to play after a download (Balatro, 60 MB):** automatic preparation ended at "Play · Ready to play" and "PS5
  title · Check again · Waiting for the PS5, which adds the game to Home when Playden quits". The prompt "Balatro is
  ready to play" (Later / Play, Play focused) opened on its own over Details. Play quit to Home, the "Balatro (Steam)"
  tile appeared within about 15 s, and the game started from Home.
- Seen: Install on "140" read 610 MB although 87 MB were installed; the download estimate from Steam's app info looks
  off (not investigated).

## Not verified

- Play filling with the accent during the template copy (nobody was on Details then).
- The Settings "Kept game data" manager with entries.
- A new QR code after a refused session on the console (host-tested).
