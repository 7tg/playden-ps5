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
