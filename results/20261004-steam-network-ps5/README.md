# Native Steam transport gate

The packaged FAKE77031 title completed the credential-free networking probe on
PS5 and exited with status 0. This is native-title execution evidence, not an
upload/build-only or host-only result. No user credentials were involved.

| Check | Native result |
| --- | --- |
| Nonblocking socket preflight | Both native and POSIX receive return EAGAIN immediately |
| DNS | Pass, 4 ms |
| Explicit cancellation | CURLE_ABORTED_BY_CALLBACK, 238 ms |
| Total timeout | CURLE_OPERATION_TIMEDOUT, 403 ms |
| Verified Steam HTTPS | HTTP 200, verification result 0, 278 ms |
| Verified CM WebSocket upgrade | Pass, 47 ms |
| Anonymous CM ClientHello | Sent successfully |
| Live peer response | Matching fixed-nonce PONG, 20 ms |
| Cleanup and title exit | Pass, exit 0 |

The PS5 socket path rejects fcntl/ioctl nonblocking setup with EACCES. The narrow
curl patch uses the proven native socket option and disables curl's incorrect
SOCK_NONBLOCK creation shortcut, ensuring the native setup is actually called.
The option was derived from local PS5/Vita source evidence and verified on this
console; this run does not establish support on every firmware.

The manifest freezes the executable, source inputs, and dependency artifacts.
The JSONL contains only fixed stage labels, numeric results, and counts—no IPs,
endpoints, QR challenges, tokens, account IDs, or payloads. The peer liveness
check requires a complete matching PONG; CURLE_AGAIN cannot pass it.

This closes the native transport feasibility gate. It does not prove Steam QR
authentication, session restoration, account ownership retrieval, UI acceptance,
or 4K performance. Those remain separate work and verification.
