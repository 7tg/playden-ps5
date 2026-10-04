# Steam backend decision gate

## Status

**Selected for v1: pinned libcurl 8.22.0 with Mbed TLS 3.6.6, the bounded native resolver/nonblocking
adapter, and the reduced schema-driven protobuf codec.** The packaged PS5 title passed the complete
credential-free transport gate on 2026-10-04. This decision is scoped to the tested native-title
toolchain and console environment; the inferred libSceNet nonblocking option remains guarded by a
startup preflight rather than treated as a portable PS5 ABI.

## Candidates

| Candidate | HTTPS/WSS | Protocol | PS5 cost and risk | Gate position |
| --- | --- | --- | --- | --- |
| Port the Swift source | Foundation URLSession/WebSocket | SwiftProtobuf | No supported Swift/Foundation/Keychain runtime in the pinned native toolchain | Reject |
| PS5 `libSceHttp2` plus a second custom TLS/WebSocket stack | native HTTPS; no WebSocket API exposed by the pinned SDK | reduced codec | Duplicates TLS policy and requires undocumented `libSceSsl` connection ABI or another TLS library | Hold as fallback |
| Pinned libcurl + Mbed TLS | one verified HTTPS/WSS stack, finite timeouts and multi-handle cancellation | reduced codec initially | Requires the tested PS5 resolver/nonblocking adapter and startup preflight; materially smaller than full protobuf plus Abseil | **Selected for v1** |
| Pinned libcurl + full protobuf C++ | same transport | generated C++ runtime | Pulls protobuf/Abseil and much unused reflection/runtime surface into the title | Reject for v1 unless reduced codec fails fixtures |

The pinned PS5 SDK exports POSIX socket calls from `libkernel`, an imported
`getaddrinfo/freeaddrinfo` ABI from `libScePosixForWebKit`, and native `sceNet*` resolver/socket
calls. The imported `getaddrinfo` crashed the probe title after both `sceNetInit` and curl global
initialization, before returning a resolver code. The probe therefore uses the payload libc's
documented underlying sequence directly: one bounded `sceNetPoolCreate`, `sceNetResolverCreate`,
and `sceNetResolverStartNtoa`. Resolved addresses are passed to curl through `CURLOPT_RESOLVE`; URLs
retain their original host names so TLS host verification is unchanged, and addresses are never
logged. Native resolution uses a five-second per-attempt timeout and one retry rather than the
libnet default. The six-argument call shape is cross-checked against the public Vita SDK declaration;
the timeout's microsecond unit and the matching resolver lifecycle come from the pinned payload
SDK's PS5 libc implementation provenance. With one retry, DNS work is bounded to two attempts and
approximately ten seconds before local cleanup. It exports HTTPS through
`libSceHttp2`, including explicit phase/total timeouts and `sceHttp2AbortRequest`, but its stubs have
no general WebSocket client. This made a single portable HTTPS/WSS library the smallest coherent
candidate for the native evidence gate.

The PS5 toolchain identifies as FreeBSD, but several declared libc functions are absent from the
title stubs. The probe build therefore disables curl's unused netrc parser and records four audited
feature overrides: no `getpwuid_r`, no `gmtime_r` (curl's `gmtime` copy fallback is used), no
`if_nametoindex` (named IPv6 zone IDs are outside the Steam hostname path), and no `pipe2`. Native
evidence showed that both the title libc's `fcntl(F_SETFL, O_NONBLOCK)` and `ioctl(FIONBIO)` report
failure; a receive on the same connected socket then blocked until the peer closed it. Native DNS
and `poll` timing passed in the same title. This isolates the current failure to setting a
libSceNet socket nonblocking rather than DNS, polling, or the monotonic clock.

The successful bounded experiment uses `sceNetSetsockopt`. The public Vita/PS4 ABI assigns `SO_NBIO`
option `0x1100` at level `0xffff`. The supplied PS5 ProsperoEden native client uses options
`0x1105`, `0x1106`, and `0x1109` for send, receive, and connect timeouts, where the Vita/PS4 block
uses `0x1005` and `0x1006`. The probe therefore derives `0x1200` as the PS5 nonblocking candidate
from that observed `+0x100` shift. This is explicitly an experimental ABI inference, not a public
PS5 declaration or a portability claim across firmware. Before curl performs any request, a
bounded loopback preflight calls the native option and requires an immediate `EAGAIN` or
`EWOULDBLOCK` while the peer remains open. Failure records the native return and errno, cleans up,
and exits before curl initialization. The first successful preflight also exposed a separate curl
configuration issue: the cross headers define `SOCK_NONBLOCK`, so curl added that flag at socket
creation and skipped `curlx_nonblock`; the title socket ignored the creation flag and remained
blocking. The hash-verified patch disables curl's creation-flag shortcut for this PS5 build and
routes `curlx/nonblock.c` through the same native call proven by the preflight. This exact build
then completed timeout, cancellation, HTTPS, WSS and peer-liveness checks without blocking.

The PS5 curl build also disables IPv6, curl's threaded resolver, and curl's multi-handle wakeup
pipe. Steam hostnames in this probe are resolved by the bounded native IPv4 resolver above, then
attached through `CURLOPT_RESOLVE`. The title's single-threaded polling loop never calls
`curl_multi_wakeup`; cancellation removes the handle directly on the same thread. In curl 8.22,
the configured `HAVE_PIPE` path makes `Curl_wakeup_init` call the title's unavailable pipe adapter,
and multi initialization fails before a network connection. Disabling that unused wakeup facility
removes the exact failing path without weakening certificate or host-name verification.

## Probe dependency set

- curl 8.22.0, SHA-256
  `f7ef3ae8a22e521f289803fe93543eb64c329b58aa73a9e224dfd915a2a5f4f7`;
- Mbed TLS 3.6.6 LTS, SHA-256
  `8fb65fae8dcae5840f793c0a334860a411f884cc537ea290ce1c52bb64ca007a`;
- Mozilla CA extract dated 2026-09-25, SHA-256
  `a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505`.

The probe disables proxies, verifies both peer chain and host name, caps response bytes, uses finite
connect/total timeouts, removes canceled easy handles through libcurl's multi API, and never emits
resolved addresses or CM endpoints. Production must retain certificate verification and ship a
pinned CA bundle with an explicit refresh policy.

## Production file map

The evidence gate authorizes this smallest production backend shape:

| File | Responsibility |
| --- | --- |
| `src/steam/backend/http_client.{h,cpp}` | bounded verified HTTPS and WSS connection factory over pinned libcurl |
| `src/steam/backend/proto_wire.{h,cpp}` | varint/fixed/length-delimited reader/writer with depth and byte limits |
| `src/steam/backend/auth_messages.{h,cpp}` | reduced generated-style auth message encoders/decoders |
| `src/steam/backend/cm_messages.{h,cpp}` | reduced CM header/logon/license/PICS messages and EMsg constants |
| `src/steam/backend/auth_client.{h,cpp}` | Begin QR, server-paced poll and access-token renewal |
| `src/steam/backend/cm_transport.{h,cpp}` | WSS lifecycle, binary messages, close/cancel and heartbeat |
| `src/steam/backend/cm_client.{h,cpp}` | framing, bounded jobs, pushes, reconnect policy and PICS batching |
| `src/steam/backend/library_client.{h,cpp}` | `GetOwnedGames`, entitlement merge and sanitized model mapping |
| `src/steam/backend/json.{h,cpp}` | strict bounded parser for the few Steam JSON responses, or a separately pinned small parser |
| `src/steam/session_store.{h,cpp}` | account-generation secret envelope over platform storage |
| `fixtures/steam/auth/` | synthetic QR begin, poll-pending, poll-success and renewal protobuf/JSON envelopes |
| `fixtures/steam/cm/` | synthetic ClientHello/logon, heartbeat, license list and PICS request/response/push frames |
| `fixtures/steam/library/` | bounded owned-games JSON and entitlement-merge cases with sanitized artwork metadata |

`SteamService` owns a worker thread and bounded event queue. The rendering thread only calls the
service's nonblocking command and pump methods. Each operation captures an account generation;
completion checks that generation before any save or event. WSS cancellation removes the active
multi handle and tears down the connection. Secrets use move-only/zero-on-release buffers where
practical and never enter event descriptions, diagnostic errors or published fixtures. Request
URLs and bodies must never be logged.

Implementation will generate fixtures from the pinned reference `.proto` field numbers, then decode them with both the
reduced C++ codec and SwiftProtobuf in the reference test harness. Every request fixture must be
round-tripped byte-for-byte; response fixtures additionally test unknown-field skipping, truncated
varints, oversized lengths, recursion/aggregate limits, multipart PICS assembly, duplicate job IDs,
and out-of-order pushes. Tokens, refresh tokens, account IDs, QR challenges, CM endpoints and IP
addresses are replaced with deterministic synthetic values before a fixture can be committed.

## Codec decision

Start with a reduced schema-driven codec, not hand-written byte literals spread through the client.
The supported wire types are varint, fixed32, fixed64 and length-delimited. Unknown fields are
skipped by wire type; malformed lengths, recursion, multipart expansion and aggregate response size
are bounded. Implementation must generate golden frames from the exact vendored reference `.proto` files and
cross-decode them with SwiftProtobuf in an isolated reference test harness. This retains protobuf compatibility without
shipping the full C++ runtime.

Escalate to a pinned small protobuf runtime only if the golden fixture suite exposes a maintenance
or correctness failure. Full protobuf C++ is not justified by the v1 message subset.

## Native evidence closing 3A

The final packaged probe used eboot SHA-256
`001ac03c04089817b1a8110c670dcd86955ba05c03faa940b4ea232779712cd5` and exited with status 0.
Its sanitized JSON lines recorded:

- native nonblocking preflight pass: native set returned 0; native and POSIX receive returned
  `EWOULDBLOCK` immediately while the loopback peer remained open;
- DNS success in 4 ms;
- explicit cancellation in 238 ms with curl code 42;
- deterministic timeout in 403 ms with curl code 28;
- Steam directory HTTPS status 200 in 278 ms, certificate verification result 0, three sanitized
  endpoints;
- CM WSS upgrade in 47 ms with certificate verification result 0;
- the 12-byte anonymous `ClientHello` sent successfully; and
- matching fixed-nonce WebSocket PONG in 20 ms, proving peer-observable post-send liveness.

The build manifest records the curl/Mbed TLS/CA source hashes, the hash-verified PS5 curl patch,
toolchain and compiler identities, and every linked archive hash. The probe logs contain no token,
account, IP or CM endpoint. Milestone 3A is closed for the tested native-title environment.

## Transport references

- curl's official [WebSocket API](https://curl.se/libcurl/c/libcurl-ws.html) documents verified
  `wss://` support, connect-only mode, and `curl_ws_send`/`curl_ws_recv`.
- curl's official [8.22.0 release notes](https://curl.se/changes.html#8_22_0) identify the pinned
  transport release.
- Mbed TLS's official [3.6.6 release](https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.6)
  identifies the pinned TLS LTS source.
- The public Vita SDK [`sceNetResolverStartNtoa` declaration](https://docs.vitasdk.org/group__SceNetUser.html)
  confirms the native resolver's six-argument ABI. The PS5 timeout unit and lifecycle were checked
  against the [payload SDK libc resolver implementation](https://git.earthonion.com/soniciso/sonicloader/commit/bf13d09a92d5492e0eca93eca9d99121a54245f6).

Sanitized native logs and the executable/source/dependency manifest are committed under
[results/20261004-steam-network-ps5](../../results/20261004-steam-network-ps5/README.md).

## Authentication client implementation

`AuthClient` implements the existing worker-owned `AuthBackend` using the pinned reference's
`BeginAuthSessionViaQR`, `PollAuthSessionStatus`, and `GenerateAccessTokenForApp` protobuf POSTs.
It initializes HTTP lazily on the service worker. The device identifies as `Playden (PS5)` with
SteamClient platform type and the reference's `os_type=0`/`website_id=Client` fields.

Requests have a 10-second maximum and a 64 KiB response cap. QR attempts have a 300-second
monotonic lifetime; polling waits at least one second, honors a longer server interval, and
caps each request/wait to the remaining lifetime. Cancellation is checked around requests,
waits, challenge callbacks and renewal. Replacement challenge/client IDs are applied only after
validation. Challenge URLs are limited to the Steam `https://s.team/q/` prefix, printable ASCII,
and 4096 bytes; unexpected URL formats fail visibly rather than producing a misleading QR.

As in the reference, an absent `X-EResult` header means success; malformed or duplicated headers
are protocol failures. HTTP/server failures are stable error categories with no response text.
Restoration revocation clears only the stored-session path: a rejection while obtaining the
access token for a fresh QR attempt must not revoke a previously saved account.

JWT inspection is bounded local parsing, **not signature verification**. Tokens come from
certificate-verified Steam responses, and restoration always calls the server. The parser
requires canonical base64url segments, an exact positive uint64 subject and positive integer
expiry, rejects duplicate JSON keys and malformed UTF-8, and never converts Steam IDs through
floating point. AuthClient requires unexpired tokens with matching subjects, validates account
names as nonempty UTF-8, and validates rotated refresh tokens before replacing session data.

Synthetic tests cover replacement, rejection, retry, cancellation, expiry, deadline budgeting,
malformed responses/headers/tokens, renewal rotation and failed replacement preserving the
previous repository through `SteamService`. The tests pass on host, including UBSan, and the
implementation cross-compiles for PS5. This is protocol implementation evidence only: graphical
QR wiring, real PS5 phone approval, restart restoration, and sign-out acceptance remain pending.
