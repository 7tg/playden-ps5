# Steam reference audit

## Reference boundary

This audit uses the local `../playden` repository at
`de952b3d49167e635945ada88cd8533e07d40d44`. The requested branch name
`feature/launcher` was not present; the available reference was `feature/launchers`. Files were
read with `git show <revision>:<path>` without switching or modifying that checkout.

The relevant macOS implementation is split across these files:

| File | Responsibility | Direct portability issue |
| --- | --- | --- |
| `SteamCore/SteamAuth.swift` | QR/password auth, server-paced polling, access-token renewal | Swift async tasks, Foundation dates/data |
| `SteamCore/WebAPI.swift` | HTTPS protobuf and JSON calls | `URLSession`, Foundation JSON |
| `SteamCore/CMTransport.swift` | persistent WSS message transport | `URLSessionWebSocketTask`, `NSLock` |
| `SteamCore/CMClient.swift` | CM framing, logon, heartbeat, jobs, pushes, reconnect boundary | Swift actor/tasks, SwiftProtobuf |
| `SteamCore/CMDeviceIdentity.swift` | stable per-install login ID and machine ID | CryptoKit SHA-256/SHA-1 |
| `SteamCore/Library.swift` | `GetOwnedGames` JSON validation and mapping | Foundation JSON/dates |
| `SteamCore/Entitlements.swift` | licenses to package PICS ownership and acquisition dates | SwiftProtobuf, binary KeyValues parser |
| `SteamCore/PICS.swift` | access tokens, product info and VDF metadata | SwiftProtobuf, VDF parser |
| `SteamCore/TokenStore.swift` | token model and file adapter | Foundation Codable/files; explicitly a PoC adapter |
| `Sources/SteamBackend.swift` | app-facing auth/library composition | Domain models and Swift concurrency |
| `Sources/SteamAccount.swift` | Keychain boundary, generations, shared CM lifetime | Security Keychain, CryptoKit, actors/tasks |
| `App/AccountModel.swift` | QR/approval/cancel/retry UI state | AppKit/SwiftUI presentation only |
| `App/LibraryModel.swift` | bounded app state and sync orchestration | SwiftUI/Observation presentation only |

`AccountModel` and `LibraryModel` provide useful state-machine behavior, but no transport or storage
code should be ported from them. Secrets remain below `SteamAccount`; the UI receives only identity
and authentication-state events. Canceling or replacing an attempt changes a generation before its
task is canceled, so a late result cannot establish the replaced session.

## Smallest v1 chain

1. Send an HTTPS protobuf request to
   `IAuthenticationService/BeginAuthSessionViaQR/v1/` with device details, website ID `Client`,
   Steam-client platform type, and a persistent-session request.
2. Display only the returned `challenge_url`. Retain `client_id`, opaque `request_id`, and the
   server-provided poll interval below the service boundary.
3. Poll `PollAuthSessionStatus` at `max(interval, 1 second)` for at most five minutes. Replace the
   client ID and QR challenge when the server supplies new values. Approval yields refresh and
   access JWTs plus account name.
4. Persist the refresh token, optional access token, account name, Steam ID derived from the JWT,
   and optional cell ID in account-scoped secret storage. Never log these fields. Access-token
   expiry can be inspected locally, but the token signature is not treated as locally verified.
5. Renew an expired access token through `GenerateAccessTokenForApp` using the refresh token.
6. The reference fetches `IPlayerService/GetOwnedGames/v1/` over verified HTTPS with the access token,
   `include_appinfo=true`, and `include_played_free_games=true`. This is the minimum library list and
   provides stable app ID, name, playtime, last-played time, and stable CDN artwork paths without a
   public profile. Valve's public documentation requires a publisher Web API key; access-token-only
   use is reference-observed, undocumented behavior and cannot be the authoritative production
   ownership path without native validation.
7. For account ownership metadata that the reference considers authoritative, fetch
   `ISteamDirectory/GetCMListForConnect/v1/?cmtype=websockets`, establish
   `wss://<endpoint>/cmsocket/`, send `ClientHello`, and log on with the **refresh** token. Supply a
   stable random per-install device identity so simultaneous Playden sessions do not replace one
   another unexpectedly.
8. Retain the CM connection, honor the logon heartbeat interval, and receive the pushed license
   list. Filter inactive, expired, borrowed, canceled, fraud, and foreign-owner licenses.
9. Batch package PICS requests with each license access token. Parse each bounded binary KeyValues
   package response to obtain owned app IDs and earliest acquisition dates. This path supplements
   `GetOwnedGames`; it must not turn a failed metadata request into an empty owned library.
10. App PICS is required only for metadata absent from `GetOwnedGames` (later install/depot work).
    The v1 card/details model should prefer the smaller HTTPS library result and add bounded PICS
    fields deliberately rather than importing the download stack.

## Wire subset

HTTPS auth needs these messages and fields:

- `CAuthentication_DeviceDetails`: `device_friendly_name`, `platform_type`, `os_type`;
- `BeginAuthSessionViaQR` request: device details and `website_id`; response: `client_id`,
  `challenge_url`, `request_id`, `interval`, confirmations;
- `PollAuthSessionStatus` request: `client_id`, `request_id`; response: replacement client/challenge,
  refresh token, access token, account name, and guard data;
- `AccessToken_GenerateForApp` request: refresh token, Steam ID, renewal type; response: access token.

CM messages are framed as little-endian `uint32(EMsg | 0x80000000)`, little-endian protobuf-header
length, `CMsgProtoBufHeader`, then protobuf body. The v1 subset is:

- `CMsgClientHello` (`protocol_version=65580`, EMsg 9805);
- `CMsgClientLogon` / `CMsgClientLogonResponse` (EMsg 5514 and server response), including account
  name, refresh token in `access_token`, cell ID, client/session fields and device identity;
- `CMsgClientHeartBeat` (EMsg 703), `CMsgClientLoggedOff`, `CMsgClientLicenseList` (EMsg 780);
- `CMsgMulti` (EMsg 1) with bounded gzip expansion;
- `CMsgClientPICSProductInfoRequest/Response` (EMsg 8903/8904) for packages;
- `CMsgClientPICSAccessTokenRequest/Response` (EMsg 8905/8906) only if app PICS is used;
- `CMsgProtoBufHeader` fields for Steam ID, session ID, source/target job IDs, target job name and
  EResult.

The probe's anonymous hello is exactly 12 bytes: EMsg 9805 with the protobuf bit, a zero-length
header, and `CMsgClientHello.protocol_version=65580`. It proves the transport/framing path without
credentials.

## Protocol source and dependency audit

The checked-in Swift generated sources say they came from the vendored `.proto` files under
`Packages/SteamKit/protos/steam/`. `regen.sh` identifies
`https://github.com/SteamDatabase/Protobufs` as upstream and uses protoc plus
`protoc-gen-swift 1.38.1`. It does **not** record the upstream Protobufs revision or hashes, and it
locally changes one unused enum value. A C++ port must pin and hash its own reduced `.proto` inputs;
the checked-in reference files are the reproducible source for this port until that provenance gap
is repaired upstream.

The Apple-only/runtime dependencies to replace are:

- Swift and Swift concurrency (`actor`, `Task`, cancellation handlers, continuations);
- Foundation (`URLSession`, `URLSessionWebSocketTask`, `Data`, JSON, Codable, files, dates);
- SwiftProtobuf 1.38.1 at revision `55d7a1cc5666b85c13464aea1c4b4a90feccb4c8`;
- CryptoKit SHA-256/SHA-1;
- Security Keychain through `KeychainCredentials`;
- AppKit, SwiftUI and Observation in the presentation models.

`CLzma`, `CZstd`, depot download, manifests, Cloud, PE preparation and the bundled Steam API files
are outside the connected-library v1 path. CM `CMsgMulti` still needs gzip/deflate support or an
explicit bounded alternative.

## Behavioral constraints retained from the reference

- HTTPS request timeout is finite (30 seconds per request, 60 seconds per resource in the reference).
- QR polling uses the server interval and expires after five minutes.
- A generation invalidates late auth, sync and renewal results before credentials are saved.
- Sign-out cancels work, closes CM, then clears credentials and account-scoped library state.
- A dropped CM may reconnect with a small bound; `LogonSessionReplaced` must not start a kick loop.
- Credentials and package/depot access tokens stay inside the Steam adapter.
- Responses have byte, nesting, entry and multipart limits. Malformed/incomplete library data
  preserves the prior valid snapshot instead of committing an empty result.

## Primary references

- Valve's [`IPlayerService.GetOwnedGames` documentation](https://partner.steamgames.com/doc/webapi/IPlayerService#GetOwnedGames)
  defines the public HTTPS library operation, its app-info/playtime fields, and its publisher-key
  requirement. The reference's access-token-only call is explicitly treated as undocumented.
- The exact Steam message schemas audited here are the generated and `.proto` sources stored at the
  pinned local reference revision above. Their `SteamDatabase/Protobufs` upstream is recorded for
  provenance, but is not accepted as a reproducible input until an upstream revision and hashes are
  pinned.
