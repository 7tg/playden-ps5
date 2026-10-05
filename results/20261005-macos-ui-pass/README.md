# macOS UI pass on the PS5

Title `FAKE77031` on the dev PS5, signed into the real Steam account, deployed over zftpd (SHA-256 verified) and launched with the PS5 MCP. Reference: the macOS app at `../playden` `de952b3` and the user's screenshots and recording. Screenshots are not committed because they show the account name.

Host suite: 63/63, PS5 package builds.

| Change | Console check | Result |
| --- | --- | --- |
| Tile focus without tint | Library, Castle Crashers focused | **Pass.** Cover untinted; orange ring and glow outside the tile; title and "Not installed" over a soft gradient |
| Focus cross-fade | Library, Right, snapshot 60 ms later | **Pass.** Old tile's ring fading out while the new one fades in |
| Install state on tiles | Library and Home | **Pass.** Download marks on games not installed; size and playtime on 140 ("87 MB · 7 min played"); Installed section with 2 games |
| Home scrolling | Home, Left then Down | **Pass.** Left moves the highlight without moving the shelf; Down scrolls only enough to show the focused shelf and its title |
| Details banner | The Talos Principle | **Pass.** Steam library hero across the top with the game's logo; Install 5.7 GB as the large primary button beside Favorite and More; facts as label-over-value cells |
| Details focus | Open a game not yet checked | **Pass.** Focus moves to Install once Steam's details arrive |
| Settings sections | Settings, Down twice, Left, Down | **Pass.** Section rail; Library shows Games storage ("Console storage ›") and Runtime status; the rail moves to Display with the Reduced motion toggle (Off) |
| Tabs and type | All root screens | **Pass.** Icon tabs with the current one in an outlined box; Downloads at the macOS sizes |
