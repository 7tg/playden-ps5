# Per-game PS5 title name and icon — 2026-10-06

Builds `e42a984` and `6fa03f1` on FW 13.60, title `FAKE78002`.

- Update rewrote the title's `sce_sys/param.json` name to "Half-Life 2" and `icon0.png` with a 512x512 icon made from
  the Steam cover ([icon](icon0.png)). `runtime.log`: `title metadata app=220 name=Half-Life 2 icon=240158B`, refresh
  state 0. No new registration from Playden.
- The PS5 keeps the name and icon it read at registration (ShadowMount reads them only on a first install), so the title
  was registered again from the build host: ShadowMount's API was bound to the LAN for the procedure
  (`config.ini` backed up, SHA-256 `0a234020…`), `games/uninstall` FAKE78002, ShadowMount re-registered it on its own
  within 5 s, the manual source it disabled was re-added, and `config.ini` was restored byte-for-byte; the LAN API then
  refused connections. Home showed "Half-Life 2" with the new icon ([Home](home-half-life-2.jpg)); the title launched
  HL2 and quit cleanly; saves in the game folder were untouched; the PS5 play-time counter restarted at 0.
- Settings > Library "Store name in PS5 titles" ([setting](setting-store-name.jpg)): turning it on made HL2 report
  Update; Update wrote "Half-Life 2 (Steam)"; after the same re-registration Home showed it
  ([Home](home-half-life-2-steam.jpg)).
- The header no longer draws the orange account dot ([header](header-without-dot.jpg)).

Limits: Playden cannot re-register a title itself (ShadowMount refuses while any app, Playden included, runs), so a
renamed prepared title shows its new name on the PS5 only after a re-registration outside Playden. New titles get the
right name and icon from their first registration. Observed bug: after leaving Settings with L1, the Home focus ring was on
Half-Life 2 but Cross opened another game until the focus was moved.
