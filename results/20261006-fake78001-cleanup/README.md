# Removal of the leftover FAKE78001 title — 2026-10-06

`FAKE78001` was the isolated bootstrap acceptance title (install `acceptance-hl2-fresh-v16`), unused since Half-Life 2
moved to `FAKE78002`. Procedure, all through ShadowMountPlus's own API:

1. Backed up `/data/shadowmount/config.ini` (SHA-256 `0a234020…`) and bound the API to the LAN (`api_bind_address=0.0.0.0`,
   reloaded live).
2. Backed up the source folder (10 files, deployed copy) to `build/console-backups/fake78001-cleanup/` (not committed).
3. `manual/remove` `/data/homebrew/FAKE78001` → `changed: true`; deleted the source folder over FTP.
4. `games/uninstall` FAKE78001 **with Playden in the foreground** → `{"status":16,"error":"Device busy"}`, although the call
   came from the LAN, not from Playden: ShadowMount refuses uninstall while any app runs, not only when the caller is one.
5. Closed Playden (console at Home, no app running) → `uninstall_requested: true`.
6. After a scan: FAKE78001 absent from `/api/v1/games`, the console's app list, `/user/app/FAKE78001` and
   `/data/homebrew/FAKE78001`; it did not come back.
7. Restored `config.ini` byte for byte (read-back hash `0a234020…`); the LAN API refused connections again.

No native database was edited by hand. Not removed (they are private to their titles and unreadable over FTP and the web
file manager; deleting them is a decision for the removal feature): the bottles `/data/playden/games/acceptance-hl2-fresh-v16`
(may hold HL2 test saves), its `.ftp-staging` copy and `probe-session-1073/1084/1089-*`. The real HL2 bottle `steam-220`
and `FAKE78002` were not touched.
