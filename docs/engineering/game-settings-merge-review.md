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

## Final settings handoff review — 2026-10-05–06

Reviewed the six follow-up commits through `8f81a3d` against integration
`a84710d`. The independent review found preset identity, borrowed recipe
lifetime, captured registry application, publication recipe identity,
Environment capability gating, rollback diagnostics/mode preservation and
missing durability barriers. Regressions reproduced these before correction.

The launch plan now owns its selected entry and app/recipe identities. It
requires the record's exact preset (empty records accept null or the canonical
empty default), applies captured registry values and stamps captured recipe
identity into publication. Environment uses its own capability gate. Atomic
file replacements preserve modes and sync the file and directory; rollback
covers delete/recreate and reports restore/remove failures distinctly.
The bundled input presets now retain their upstream MIT notice.

The settings helper is explicitly a stopped-title file operation, not a
crash-atomic multi-file transaction. The integration owner must journal settings
apply plus profile/descriptor publication, validate the generated profile and
input presets with the pinned parser, and block readiness until failures or
interruptions are reconciled. These production adapters remain open.

Final combined host build and all 75 CTest tests pass, including syscall failure
injection for file/directory sync and failed rollback rename/remove. The PS5 SDK
package build passes; its eboot SHA-256 is
`109e55a60d1e554104ba787421cce77ef2c153acc2379284cf5364af6d1552b1`.
The package contains the input presets and their MIT notice. The re-review also
caught private registry backup permissions; backups and temporary files now
inherit the source mode before receiving data, with a first-backup regression.
No console claim or deployment occurred during this merge review. The existing
working preview remains installed.

The final settings handoff and review corrections are merged at `37fa032`.

## Input/audio and navigation handoff — 2026-10-06

Reviewed and merged `feat/game-settings` through `47db605` into the integration branch. This adds capability-gated trigger dead zone, mouse wheel and per-game volume, the settings-rail Quit action, and the bounded Continue playing shelf with a Library link. The retained Wine runtime does not advertise these new features: parser and UI support alone cannot enable them.

Review reproduced an admission gap: unknown required runtime feature names were treated as supported. The corrected filter refuses unrecognized features/version requirements and malformed requirement arrays. Legacy numeric `#PR` references remain informational provenance. New regressions cover an unknown feature even with every known kit capability enabled and non-string/empty/non-array requirements.

The combined tree passed all 76 host tests and the full PS5 SDK package build with NativeAccess enabled and the developer probe disabled. No deployment or hardware acceptance of the new navigation/settings changes occurred. The separate patched Wine input/audio runtime still needs a build, provenance review, and PS5 acceptance before its capabilities may be advertised. Production live settings/publication/Play adapters remain open.
