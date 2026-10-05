# Per-game wrapper acceptance: in progress

On 2026-10-05 the SDK 0.42 one-shot
[registration bridge](../../probes/per-game-title/README.md) ran as payload PID
761 while the console was idle at Home. The PS5 was released after collection.

[Redacted capability log](registry-capability-probe.log) establishes:

- ShadowMountPlus `/api/v1/games` returned HTTP 200 and status 0.
- The production registry adapter reported FAKE78001 absent from its cache.
- `fsync` of the owned development directory succeeded, and `/dev/urandom`
  returned 16 bytes in the payload process.

The operator also observed both `/user/app/FAKE78001` and
`/data/homebrew/FAKE78001` absent in separate FTP checks before the probe. These
checks are not recorded in the linked payload log. The original inventory
response body is omitted from that log to avoid publishing unrelated titles.

No `register.request` was created, so no title registration was attempted.
Only the owned development marker/log and Payload Manager diagnostic ELF were
added. PPSA99995 and its working bottle were unchanged.

These checks establish the bridge and payload API availability. Per-title helper
behavior, argument-free Home startup, actual game rendering, session lifecycle,
error handling and exit-to-Home acceptance remain open. The fresh distributable
bottle and immutable kit are also still in preparation.
