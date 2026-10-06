# Durable settings request prerequisite, 6 October 2026

Initial preparation now writes a bounded immutable PWRQ1 settings request into
an existing private OWNER request directory before arming the OWNER record and
before control setup or before-state capture. The request binds the complete
reserved identity, kit identity, requested plan and registry timestamp with a
canonical digest. It contains no receipt or before-state ownership proof.
A pre-arm orphan cannot authorize capture or be implicitly overwritten/adopted.

The operation and request codecs share the same identity/plan field encoding;
PWOP2 operation bytes remain unchanged. Requests require exact EOF, bounded
fields, matching identities and supported profile/input contents. Recovery files
require current-EUID ownership, exact mode 0600, one link, stable directory/file
identity and a transaction-matching filename. Existing requests are never replaced.

Cold repository/service tests recover the original initial plan before control
creation and after control creation but before snapshot capture. No recipe reload
is required. Changed requested bytes, unknown activity and pre-arm orphans refuse
mutation. Ready-title updates retain the existing Prepare API; their durable
request handoff remains to be implemented, as does explicit orphan reconciliation.

An integration regression also exposed a stale native submission: publication
persisted Registering but passed its old frozen record to Register. The submission
now uses the persisted armed record. The stricter regression failed before the fix
and passed afterward. Full host suite: 108/108. The OWNER/publication SDK v0.43
archive compiled, and the actual developer package linked the request codec/store;
this uncovered and repaired the missing game-path archive in the manual SDK link.

Native PID 1168 on FW13.60 created a fresh private diagnostic request directory,
encoded/wrote/synced/reopened the 770-byte request, verified exact roundtrip and
refused overwrite. This synthetic storage probe did not register a title, publish
settings or validate the whole native OWNER coordinator. The normal preview was
restored and independently read back with 0755/0644 modes; console released.
Original kit/application/UI, native publisher/Wine lifetime and gameplay gates
remain open. verification.json records exact artifacts and scope.
