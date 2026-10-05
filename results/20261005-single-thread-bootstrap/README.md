# Startup sequencing investigation — 2026-10-05

Deferred display initialization until after bootstrap. The graphics preparation
callback now probes the linked EGL entry point with `eglGetDisplay`; in the
linked implementation this returns a static display without initializing EGL.
`main` still checks the actual display open before constructing the UI.

Split networking initialization from its watchdog-based nonblocking preflight.
Bootstrap uses only the basic process-wide `sceNetInit`; Steam/curl callers
still run `InitializeNativeNetwork`, which performs the existing preflight once.
Networking is retained for the process lifetime, as before.

The first experiment deferred display initialization alone. It still failed
Lapy's target preflight with two threads. The final experiment also deferred the
networking preflight; its [helper log](lapy-live.log) still reports
`target_preflight error=16`, two threads, and no changed roots or credentials.
The [startup log](native-startup.log) confirms the detailed failure. These tests
rule out EGL initialization and the networking watchdog as the sole cause;
basic network initialization, UserService initialization or earlier module
startup remain candidates. The helper's ownership checks were preserved.

The final eboot SHA-256 is
`95c6448915020b56a84632cd1dc85eab976aaf963b8bd687f8806c72ea8981c4`.
It was uploaded through zftpd, read back to verify the SHA-256, chmodded 0755,
and launched successfully. Home rendered with no account name, cached artwork
and the installed 140/Half-Life 2 entries. The upload MCP first timed out;
remote hashing proved it had left the previous executable intact, so that
initial launch was not counted as a test of the new build.

All 74 host tests and the PS5 SDK build pass. Native runtime readiness remains
false; per-game launch and installation are not enabled by these changes.
The app was closed for the user to relaunch with their own controller, and the
console claim was released. The previous working eboot was retained locally
in `build/settings-merge-ready/preview-before-single-thread.bin`.
