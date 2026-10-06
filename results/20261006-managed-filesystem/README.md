# Native managed filesystem diagnostic — 2026-10-06

FW13.60, Playden FAKE77031, normal Lapy grant followed by restored EUID 1.
Probe executable SHA-256: `82a30c562c14ff64fe7b33163a22cc7e294fe2541c4dc6fcad7655ed6b888495`.

All logged production backend/directory/hash checks passed. The fixture left
`/data/playden/managed-fs-1046-1791245713/library/prefixes/game/proof.txt`
containing `abc`, verified natively against the known SHA-256. FTP could not
read that private path. No inference about power-loss persistence or the
unmigrated settings/install consumers follows from this diagnostic.

The console's existing preview executable and package identity were backed up
before the probe and restored afterward. Restored executable SHA-256:
`74d299ebb3a5d8222128f12822d355faf4aa4be0e263bb9f9a7ede7000ce8cb2`.
The default package was rebuilt with `PLAYDEN_TITLE_CONTRACT_PROBE=OFF` and
matched that executable hash. Console ownership was released.

## Durable journal increment

A second diagnostic invoked the actual `Storage::WriteDurable` and
`ReadDurable` operations in a fresh private tree. Publication, recovery sync,
readback and production SHA-256 all passed under EUID 1. See
[journal native log](journal-native.log). Probe executable SHA-256:
`832a450e20ce0692ae8c3cc6c050de46c4b412b6229346d37111caee214a3863`.
This validates normal successful native syscalls, not power-loss recovery.
The same normal preview was restored and the console released afterward.

Host repository tests inject file and directory fsync errors in reservation and
update, confirm no further effects/access until reload, and distinguish the old
journal from the new one already published by rename. They verify recovery sync
failure, 0600 output, symlink destination/root refusal and preservation of a
sentinel behind the legacy `.tmp` path. The original file-sync test failed
against the old writer before migration. Full host suite: 88/88 passed.
Independent review found no actionable defect; native installer provisioning of
a dedicated private journal root and full lifecycle wiring remain open.

## Settings and directory enumeration increments

[Settings diagnostic](settings-native.log), executable SHA-256
`406f512ad15eaa497c910733033ebfceab61b76e5e3eb02b7b173d0e8d421856`,
passed owned legacy `0644` → private `0600` migration, no destination overwrite,
save/reopen with changed revision, legacy source preservation and directory
listing under restored UID 1. The private directories were `0700`.

[Shared setup diagnostic](shared-settings-native.log), executable SHA-256
`f4c8c850947525ff3742267f36f3c870b8c58b80ea647b748c719065f9624473`,
additionally passed the actual shared settings setup helper and repeat selection
of its existing private record. Both runs retained the original preview backup,
restored it afterward and released the console. These are production API checks
in fresh native fixture trees, not full live game settings/launch acceptance.

The full combined host suite passed 90/90 after adding setup coverage and fixing
the live-model fixture's private-directory precondition. Review identified host
CLI relative/public-root regressions; shared setup resolves those without
relaxing low-level policy or chmodding general storage.

## Normal preview deployment after settings integration

The normal package (`PLAYDEN_TITLE_CONTRACT_PROBE=OFF`) was installed after
commit `73a4f0b` and both deployed files were independently read back over FTP:

- `eboot.bin`: `d76d1a42306daf024d792da5497c29a8152a8341d71470e86f79ab8bd4308daa`
- `playden-package.identity`: `c7593741974bfd517df3335f9ecfe6b41a17fafd4f3c0f0432ca548a6921fb40`

Native startup logged `2026-10-06T00:40:46Z game settings storage initialized`
and rendered Playden Home with the existing account and cached library. The app
was closed after verification and console ownership released. The normal package
remains installed. This does not establish game launch or gameplay acceptance;
artwork/icon work remains deferred by user request.

## Native session storage increment

[Session diagnostic](session-native.log), probe executable SHA-256
`ed9945b2f4417a720d939501ac7d02cc9cf15e723d9ce469f0e4092b9b75bf46`,
passed under EUID 1: fresh private canonical game tree, complete marker and
synthetic wrapper JSON publication, actual production session enumeration,
separate private reconciliation journal save/reopen, and refusal of retained
pending lock evidence. Existing managed filesystem/settings checks also passed.
The fixture records are synthetic; actual pinned C writer compatibility is
covered on the host. No game registration/launch or Cloud operation occurred.

The first final native link failed because `std::ostringstream` pulled unsupported
libc++ locale functions into the session adapter. Escaped string construction and
integer `std::to_string` preserve the JSON contract without those imports. The
final probe package linked and the focused host suites passed 3/3 after this fix.
SDK archive compilation alone had not exposed the missing imports.

The prior normal preview and identity were restored and read back independently
with the exact hashes documented above. Console ownership was released. Full
session lifecycle, Cloud/controller wiring and crash/power-loss recovery remain
open. The private diagnostic fixtures and pending evidence were left intact.

## Durable launch journal and locking qualification

[Launch/lock diagnostic](launch-lock-native.log), probe executable SHA-256
`056e5c15b1213ee9d7f6cc1d480d4236c3a98b9d8560972bb8ea2d9eb6f0fa13`,
passed at EUID 1 in fresh private fixture trees. It invoked the actual
`StorageLaunchSessionJournal` for exclusive history, UUID collision refusal,
pending publication, restart readback with max uint64 revision, durable clear,
repeat absent clear and recovered absence. No native game handoff occurred.

A fresh owned `0600` regular file with independent open descriptions verified
`SYS_flock` (131) through the previously checked kernel gateway: simultaneous
shared readers, a blocked nonblocking exclusive writer, release on reader close,
a blocked shared reader while exclusive ownership is held, and release on writer
close. This qualifies that primitive in this native filesystem environment. It
is not cross-process wrapper/publisher acceptance or proof of a complete guard.
The fixture lock inode was never renamed or replaced.

The first executable upload timed out and independent readback showed the prior
normal binary remained, so that launch was excluded from acceptance. Direct FTP
upload/readback then verified the exact probe hash and `0755` permissions before
the diagnostic launch. The normal executable/identity were restored and verified
against the hashes above, and console ownership released.

The combined host suite passed 95/95. Durable storage/launch focused tests also
passed 4/4 under independent review, covering real file/directory sync errors,
private root/symlink refusal, immutable history and stopped retry behavior after
uncertain publication. New APIs preserve the general Steam storage behavior.

## Production lock adapter

[production-lock-native.log](production-lock-native.log) records fresh PID1089,
EUID1, root `/data/playden/managed-fs-1089-1791249449`. Probe binary SHA-256:
`86d58f5f6fcfa6598beae1150264fee5f056f68c70165d4c76c34c6c0598c9a3`;
package identity SHA-256:
`94ba6dcb7fa2de593918893525befa3e824cd8fb393beafb886e7bea76eaf12d`.
Direct FTP upload, executable mode `0755`, and independent byte readback were
verified before launch. The production `ManagedFileLock` adapter passed shared
reader/exclusive publisher contention, last-reader release, publisher release,
and missing-file no-create refusal through the checked native syscall gateway.
All previous filesystem, settings, session and launch storage checks also passed.

This fixture uses independent descriptions in one native process. It proves the
production primitive, not cross-process Home/Wine publication or child lifetime.
Host normal/native-policy suites cover refusal after identity/mode replacement,
symlinks, hard-link aliases, moves, unavailable native provider and process exit.
Full host suite: 97/97 passed. Independent source/link review found no issues.
The existing `d76d1a...` preview and `c75937...` identity were restored with full
SHA-256 readback; the application was closed and the console released.
