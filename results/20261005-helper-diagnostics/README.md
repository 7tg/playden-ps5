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

## Console diagnosis

The diagnostic eboot `08071c33a65ed19147d65930b14cd9a6d88570b39d669af1079ccb55c880342a`
was deployed and launched twice on firmware 13.60. Home rendered without the
account name. The [startup log](console-startup.log) records
`helper stage=helper-result status=7 error=13`: the validated helper reply is
`APPLY_FAILED`, rather than a socket or transport failure.

The [helper log](lapy-live.log), captured through its existing configured TCP
logger, narrows this to `target_preflight error=16` (EBUSY). The target has two
threads, while `private_process_credential` in the pinned helper requires exactly
one thread, two credential references and a matching thread credential pointer.
No roots or credentials were changed (`roots_moved=0 cred_changed=0`). The root
layout probe itself passed. The extra thread is observed after display startup;
which initialization created it has not yet been isolated.

Next implementation must establish the grant before starting additional display
threads while preserving graphics/module availability and launcher fallback.
Do not relax the helper's credential ownership checks to bypass this failure.
The final preview was closed so the user can relaunch with their own controller,
and the console claim was released after each capture.
