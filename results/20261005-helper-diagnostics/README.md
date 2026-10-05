# Helper handshake diagnostics — 2026-10-05

Upstream helper client commit `4e5e2bc` rejects a terminal helper OK after a
failed local credential clone. It adds optional local stage, validated helper
status and preserved error diagnostics without changing the wire protocol.
Existing callers remain compatible. Playden consumes the detailed API in
native startup and requires it when configuring native runtime access.

The focused macOS host test passes with warnings treated as errors. The
[Linux sanitizer run](linux-sanitizers.log) passes with GCC address/undefined
behavior sanitizers and leak detection in a disposable native ARM64 container.
The PS5 SDK build succeeds. These tests exercise stubbed network replies and
credential operations; they do not prove a working console grant.

The macOS combined ASan/UBSan executable hung during dyld/ASan initialization
before main; it was sampled and terminated. A separate macOS UBSan run passed.
Linux sanitizer results above provide the completed combined sanitizer check.

No console claim or deployment was made for this checkpoint. The existing
preview remains on the PS5. Native access still has an observed error 13;
the new diagnostics must be exercised on the console to identify its stage.
Other per-game source work remains unfinished and this is not a release pin.
