# Native access with existing state preserved — 2026-10-05

The title-scoped helper changes effective UID 1 to 0. The private session is
mode 0600. Its reported owner differs between the sandbox and stable views;
in the stable view it belongs to UID 1, and effective UID 0 receives EACCES.
After the helper succeeds, Playden now restores its original effective UID with
`seteuid`, verifies the result, and retains the granted root view. It does not
chmod/chown the saved session or weaken private-file verification.

The [startup log](native-startup.log) shows the existing session open failing
after the grant, then succeeding after effective UID restoration. UserService
initializes afterward and bootstrap reports `launcher_ready=1 runtime_ready=1`.
The latter includes successful operational write checks for managed runtime
data. This is evidence for the tested console behavior, not a general claim
about POSIX root privileges on the PS5.

Install-volume discovery now uses the verified title root instead of hardcoded
`/app0/games`. Its regression first failed with an existing installed record in
a stable title directory; the actual InstallService now loads that record and
resolves its game root there. The primary volume ID remains the same.

All 74 host tests and the PS5 SDK build pass. Final deployed eboot SHA-256:
`035691dc27685081d5545bb5754bc2c6811b8296ff56d7fd9869c3e9627efde7`.
The executable was uploaded through zftpd, chmodded 0755 and read back to verify
its hash. On firmware 13.60, the normal launcher rendered Home without the
account name, loaded the existing account/library and artwork, and showed
installed 140 and Half-Life 2. Library showed two installed games. Downloads
showed console storage available with 5.5 GB used by those games, plus available
M.2/extended storage and disconnected USB drives. No new download or game launch
was attempted during this check.

This successful native-access build is left installed for the user to inspect.
It was closed so the user can launch it with their own controller, and the
console claim was released. Per-game preparation/launch, live settings wiring,
Cloud handoff, final source/kit pin and fresh-bottle acceptance remain unfinished.
