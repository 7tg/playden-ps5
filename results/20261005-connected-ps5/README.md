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
| Restart restoration | **Failed, fixed, re-verification pending** (below) |

## Restoration defect found on the console

On relaunch the app called `GenerateAccessTokenForApp`; Steam answered HTTP 200 with **EResult 15 (AccessDenied)** for the SteamClient-platform refresh token, which the app then treated as revoked and cleared the session (sanitized `logs/auth.log`: `method=GenerateAccessTokenForApp http=200 eresult=15`). The same refresh token was accepted by the CM logon minutes earlier.

Fix (host-tested, rebuilt eboot `a9ad7455…`):

- As the macOS reference (`validAccessToken`), restoration reuses a stored access token with more than a minute left, without contacting Steam.
- When the Web API refuses renewal with AccessDenied, renewal goes through the authenticated CM session (`Authentication.GenerateAccessTokenForApp#1`). Only a refused CM logon now counts as revoked credentials.
- Auth failures append a sanitized line (method, HTTP status, EResult) to `/app0/logs/auth.log`.

The user approved a second sign-in with the fixed build and the session was saved, but another agent then used the console and `FAKE77031` stopped launching (CE-105773-3, 0x80940033) for reasons outside this build. The restart check is to be repeated once the console is restored.

## Still pending on hardware

Restart restoration with the fixed build, CM-path renewal after access-token expiry, offline startup and recovery, sign-out and account switch, preferences across restart, clean exit, and physical DualSense checks. 4K performance remains deferred.
