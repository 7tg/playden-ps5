# Saved-session access after elevation — 2026-10-05

Added an optional diagnostic reporter to LocalSessionRepository. It preserves
lazy preparation and the existing user-facing errors, reporting directory,
private probe and bounded session-load failures to the internal storage log.
It logs operation/error metadata, never session bytes or account identity.
The host regression first failed for missing reporting on an invalid storage
root. All 74 host tests and the PS5 SDK build now pass.

The final diagnostic eboot SHA-256 is
`f380f097ef80e9eeec7d7725e80308e73acfd2dce96506f33b2442c3cb498598`.
The title was uploaded through zftpd, chmodded 0755 and read back to verify the
hash. After a successful native grant, the [storage log](storage.log) records:
`session storage load failed: could not open 'session.v1': Permission denied`.
Preparation completed, including its private-file write/read/remove probe;
the failure is reading the existing saved session, not that probe or directory
validation. This narrows investigation to saved-file access under the granted
credential/root view. The exact credential or filesystem rule remains unproven.

The earlier diagnostic runs produced no storage log because reporting covered
preparation failures only; extending it to bounded session loading revealed
the stage. No saved account contents were logged or exported for this check,
and no sign-out/reset was performed.

The prior browsing eboot
`08071c33a65ed19147d65930b14cd9a6d88570b39d669af1079ccb55c880342a`
was restored and its remote hash verified. It rendered Home and the existing
library/installed entries. The app was closed so the user can relaunch with their
own controller, and the PS5 claim was released.

Next inspect saved-file ownership/mode and the process credentials before/after
grant without logging account contents. Resolve the access difference while
preserving private-file mode verification and the existing account state.
