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
