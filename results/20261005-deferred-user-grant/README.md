# Native grant with deferred UserService — 2026-10-05

The portable bootstrap supports deferring UserService until after the grant and
before returning to UI/native launch services. Production selects this order.
Package identity is captured before the root-view change independently of
UserService. Post-grant UserService logs use the verified stable title root.

The regression first failed because UserService preceded the grant. It now
checks successful ordering/caching, ordinary browsing fallback after grant
failure, and UserService failure/retry after a successful grant without granting
again. All 74 host tests and the PS5 SDK build pass.

The tested eboot SHA-256 is
`7e4d19a2a37385de171be62e878f2e20cae8687b6be7d267ef684b5147693c20`.
It was uploaded through zftpd, chmodded 0755 and read back to verify the hash.
The [helper log](lapy-live.log) proves a one-thread target, validated private
credential, successful root transfer, reaped donors, balanced root references
and completed detach. [Startup](native-startup.log) reports helper complete,
UserService success and `launcher_ready=1 runtime_ready=1`.

Graphics initialized after the grant and rendered the connection screen, but
session storage was unavailable in the granted root view. This does not prove
account/library/Downloads survive successful elevation, and the full Task 4
acceptance gate remains open. No account reset or sign-out was performed.

For the user's preview, the prior eboot
`08071c33a65ed19147d65930b14cd9a6d88570b39d669af1079ccb55c880342a`
was restored, read back to verify its hash, and launched. Home, cached artwork
and installed 140/Half-Life 2 entries returned, proving the saved state remains.
The app was closed and the console claim released. The grant-enabled build is
available locally but is not the current console preview.

Next isolate the session storage operation that fails after elevation before
replacing the preview or connecting installation/launch as ready.
