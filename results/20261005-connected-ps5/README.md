# Connected journey on the PS5 (first real-account run)

Title `FAKE77031` on the shared dev PS5 (10.0.1.68), deployed over zftpd to `/data/homebrew/FAKE77031` and launched with the PS5 MCP. The user approved the sign-in QR with the Steam mobile app. Screenshots are not committed because they show the account name; this record contains no account identifier, token or endpoint.

## Observed

| Check | Result |
| --- | --- |
| PS5-issued QR sign-in | **Pass.** Real Steam challenge rendered; phone approval completed on the console |
| Real owned library | **Pass.** 556 games loaded into Home ("Continue playing") and Library |
| Asynchronous real covers | **Pass.** Steam CDN portraits stream in while navigating; titles without a portrait cover keep their placeholder |
| Authenticated CM | **Pass.** "Recently added" is offered only when acquisition dates exist; it appeared and sorted the library, so CM logon, license list and package PICS succeeded on the console |
| Private storage | **Pass.** `session.v1` and `steam-device-id` are 0600 (unreadable through FTP and the web file manager); library cache is schema 4 |
| Restart restoration | **Pass after fix.** With the fixed build the session restored after a full console reboot and after a clean quit, with no renewal failure logged |
| Preferences across restart | **Pass.** The "Recently added" library sort survived a reboot |
| Clean exit | **Pass.** Settings > Quit returns to the PS5 home screen without an error |
| Sign-out | **Pass.** Session, library cache and organization files deleted; device ID and global preferences kept; Welcome screen shown. The user then signed in again on the console |
| Account switch | Not run: no second account available |
| Offline startup | Host-tested only; cutting the console's network would also cut remote control |

## Restoration defect found on the console

On relaunch the app called `GenerateAccessTokenForApp`; Steam answered HTTP 200 with **EResult 15 (AccessDenied)** for the SteamClient-platform refresh token, which the app then treated as revoked and cleared the session (sanitized `logs/auth.log`: `method=GenerateAccessTokenForApp http=200 eresult=15`). The same refresh token was accepted by the CM logon minutes earlier.

Fix (host-tested, rebuilt eboot `a9ad7455…`):

- As the macOS reference (`validAccessToken`), restoration reuses a stored access token with more than a minute left, without contacting Steam.
- When the Web API refuses renewal with AccessDenied, renewal goes through the authenticated CM session (`Authentication.GenerateAccessTokenForApp#1`). Only a refused CM logon now counts as revoked credentials.
- Auth failures append a sanitized line (method, HTTP status, EResult) to `/app0/logs/auth.log`.

The user approved a second sign-in with the fixed build. Another agent then used the console, and after a reboot the homebrew payloads were not running (CE-105773-3); once the user re-applied the jailbreak, restoration passed as above.

## Still pending on hardware

CM-path renewal after an access token actually expires (unit-tested; occurs about a day after sign-in), account switch with a second account, and offline startup on the console. Live confirmation dialogs used simulation wording; fixed after this run.
