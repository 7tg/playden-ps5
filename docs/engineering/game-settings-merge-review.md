# Game settings merge review

Reviewed incoming `feat/game-settings` at `ea3c983f775327594dd97483377dde951c8290c8` against `feat/prospero-win-integration` at `1b2331b9eba6a5fcb9915a9c9850c8dca9a0546c`.

Two independent reviewers checked the settings contract and UI/persistence. No material findings remain after corrections.

## Corrections included

- Preserve device/game settings through initial account restore and sign-out. Failed reload preserves records and blocks settings autosave, while unrelated preferences and organization persistence continues.
- Announce saved settings only after successful persistence. Keep the simulated cache action out of live maintenance.
- Use exact uint64 revisions in persistent and fixture records; preserve reset revisions and reject exhaustion before mutation. Validate app IDs and recipe/sidecar integer ranges.
- Write DXVK configuration in the selected launch working directory and preserve quoted argument contents according to the actual argument parser.
- Parse Wine registry section/value names case-insensitively, accept optional timestamps and CRLF, handle missing final newlines, and reject duplicate effective keys atomically. Reject oversized/erroring registry reads before backup or replacement.
- Use descriptor-relative no-follow traversal, regular-file checks, nonblocking reads and exclusive unique temporary files for settings, DXVK, registry and shader-cache mutations. Preserve stale temporary files and reject symlink escapes.

## Verification

The combined tree, including the existing integration work, builds on the host. All 66 tests available in the current sandbox pass. A full run initially attempted all 71 tests; loopback HTTP and four macOS graphical tests cannot run with the current network/graphical sandbox permissions. Focused regression tests and strict UBSan checks pass. The combined PS5 SDK package build passes; no console claim, deployment or new hardware validation occurred.

PS5 package eboot SHA256: `cd1e0b62148affa50114ea48dd7dd66e432099a39e4a4107067df699364e1ddc`.

## Remaining integration work

The settings branch explicitly leaves S4 real Play/publication wiring open. Its settings sheet remains in simulation mode. The production adapter must use verified kit capabilities, stopped-title mutation checks and the journaled profile/descriptor publication contract before enabling live settings and maintenance. Registry consumption and the combined package still require PS5 acceptance.

## Merge and restoration status

The reviewed settings merge is committed at `249c82d` on
`feat/prospero-win-integration`. The integration stash was applied afterward.
Conflicts in PS5 startup and packaging were resolved by retaining both runtime
settings assets and native helper packaging, and by using startup-resolved paths
for settings, authentication and logs.

Fresh verification of the restored destination checkout on 2026-10-05:

- Host build succeeds; all 71 registered host tests pass, including the HTTP and
  graphical tests that the earlier sandbox could not run.
- The standalone kit-manifest test passes. These draft files are not yet
  registered as a production CMake target.
- The PS5 package build succeeds using the explicit SDK environment and the
  patched title-specific helper. No deployment or new console acceptance occurred.

The original stash is retained as a backup. Live settings and maintenance remain
subject to the integration work and acceptance described above.
