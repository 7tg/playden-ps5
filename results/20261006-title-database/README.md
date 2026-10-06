# Native installed-title inventory, 6 October 2026

The actual SDK v0.43 Playden developer title on FW13.60 independently queries
`/system_data/priv/mms/app.db` through a pinned SQLite reader. Playden and Wine
bootstrap IDs are occupied with their expected content IDs and native metadata
paths. The unused `FAKE78998` probe ID is free. `native-query-final.log` records
PID 1153, euid 1, and the three results. This verifies database observations;
it does not grant Playden ownership or native Ready.

The live database schema is captured in `observed-schema.json` without account
or complete game records. A first native file diagnostic showed the world-readable
root-owned database is accessible from the granted title context: regular file,
mode 0644, size 2772992, valid SQLite header. Its schema has `tbl_contentinfo`
with primary key `titleId`, `contentId`, `metaDataPath`, and status fields. The
reader uses bound exact title input with case-insensitive collision detection.
It deliberately makes no installed/completed claim from undocumented status values.

SQLite source/header are copied byte-for-byte from committed CheatRunner revision
59dcb9efaba71af00dc29d6cfe35da7d1ea43651; each Git object was independently hashed
and matched to the copied file and dependency lock. Source version 3.53.1, its
public-domain disclaimer and source ID are retained. Configure enforces both
file hashes. No SDK SQLite library/header was available in the current bundle.
Extension loading and URI filenames are disabled, temporary storage is memory,
and mmap is disabled. The reader serializes all connections through a nonblocking
process-wide gate; callers encountering another query get unknown.

Missing/unreadable/malformed/unsupported/busy databases and failed/incomplete
queries return unknown. Missing files are never created. Only a completely
finished empty supported query yields free. Any observed matching row occupies
an ID; duplicate case aliases and invalid/missing metadata cannot grant an
identity. Text, file size, query size and SQLite VM progress are bounded.
Read-only/no-follow mode refuses symlinked database paths. A single valid row
returns bounded content and metadata for independent ownership reconciliation.
These facts do not authorize registration or reuse of an existing title.

The empty-schema regression was observed RED, then GREEN. Fixtures cover actual
schema queries, present/absent titles, case aliases, invalid metadata, exclusive
writer contention, invalid input and no creation of missing databases. All 107
host tests pass. The actual SDK developer package links and executes the final
reader. Full logs are `build/title-database-{host-final-*,native-final-package}.log`.

The diagnostic was stopped using its freshly observed PID. Normal executable
and identity bytes were independently restored/re-read/hash verified, including
0755/0644 modes, and the console released. No database writes, bottle edits,
registration, ShadowMount restart or production trust change were performed.
Reservation must combine this inventory with source-path absence. Existing
ownership/Ready still requires immutable package/bottle markers, current OWNER
identity, native/source coherence and complete publication verification. Those
production coordinator/application links and full Wine/gameplay gates remain open.
