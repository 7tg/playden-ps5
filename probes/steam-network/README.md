# Steam native networking probe

This credential-free probe checks the network properties required by the Steam backend:

- DNS resolution for `api.steampowered.com`;
- certificate-validated HTTPS to `ISteamDirectory/GetCMListForConnect`;
- deterministic total timeout and explicit cancellation;
- certificate-validated WebSocket upgrade to a directory-provided CM and an anonymous Steam
  `ClientHello` binary frame, followed by a fixed-nonce WebSocket PING/PONG liveness check.

It prints JSON lines containing statuses, numeric error codes, counts, and elapsed times. It does
not print CM endpoints, IP addresses, QR challenges, tokens, account names, or account IDs.

Build and run on macOS:

```sh
probes/steam-network/build-host.sh
probes/steam-network/build/host/playden-steam-network-probe \
  --ca-bundle probes/steam-network/.deps/host/cacert.pem
```

The dependency script pins curl 8.22.0, Mbed TLS 3.6.6 LTS, and the Mozilla CA bundle dated
2026-09-25 by SHA-256. The native-title build must use the same sources and CA bundle. A host pass
is development evidence only; milestone 3A remains open until this exact logic passes inside the
packaged title.

Build the same pinned static dependencies for the PS5 toolchain with:

```sh
probes/steam-network/prepare-ps5-deps.sh
```

Each preparation starts from clean source, build, and install directories. The PS5 script writes
`.deps/ps5/build-manifest.txt` with source hashes, exact feature flags, SDK repository revision,
toolchain and compiler hashes/version, and hashes for all six static archives plus the CA bundle.
The launcher's package build must compare those output hashes with its tracked dependency lock;
the ignored manifest is evidence, not the trust anchor.

The callable title entry point is `playden::steam_probe::Run(const char* ca_bundle)` in `probe.h`.
Compile `main.cpp` with `PLAYDEN_STEAM_NETWORK_PROBE_NO_MAIN=1`, stage the CA bundle at an app path,
and link `libcurl.a`, `libmbedtls.a`, `libmbedx509.a`, `libmbedcrypto.a`, `libeverest.a`, and
`libp256m.a` from `.deps/ps5/lib`. With `PLAYDEN_STEAM_NETWORK_PROBE_PS5=1`, the entry point owns
`sceNetInit`, its bounded native resolver pool, curl global state, and their reverse-order cleanup;
the title must link the corresponding native imports.
