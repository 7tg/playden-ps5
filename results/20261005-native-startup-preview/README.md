# Native startup and UI preview — 2026-10-05

Deployed the normal, simulation-disabled Playden folder title `FAKE77031` through
zftpd. The final deployed eboot SHA-256 is
`c7308f71e4472812fac8368c2caa1fe9ebb73c767c4cb47929c7f075310824e2`.
The title-specific patched Lapy helper remains packaged.

The console rendered Home with no account name in the top navigation. The
existing library, artwork, and installed 140/Half-Life 2 entries were present.
This proves restoration of the existing launcher state; it does not prove a new
network authentication or game launch. Playden was closed afterward so the user
can start it with their own controller. The MCP console claim was released.

The first startup attempt exposed unsupported sandbox checks: `lstat("/app0")`
and `access("/app0", R_OK | X_OK)` returned EPERM while ordinary opens, fstat,
and log writes succeeded. Bootstrap path checks now open each path component
without following symlinks, then inspect its descriptor. Directory write checks
create and remove an exclusive temporary probe without overwriting existing
files. These component checks are not a race-proof replacement for the
descriptor-relative operations used by settings and package publication.

A separate link issue appeared when the initial correction called `openat`:
the folder-title libc PRX does not supply the SDK's static adapter. A PS5 shim
now forwards `openat` to libkernel's exported `_openat` entry. This removes that
unresolved import; it does not establish that all sandbox openat operations or
other POSIX imports are supported.

The [startup log](native-startup.log) confirms graphics succeeds and browsing
becomes available, while native data access fails with code 13. The launcher
continues using its readable sandbox paths. Native runtime readiness, per-game
preparation/launch, live settings and Cloud handoff remain incomplete.
