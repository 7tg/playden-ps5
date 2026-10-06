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
