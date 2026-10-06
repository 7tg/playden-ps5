# Frozen settings operation, 6 October 2026

The immutable operation codec and private journal store are implemented.
The canonical 2 MiB payload freezes the complete install/launch snapshot,
profile/input/descriptor/catalog/metadata bytes, before-state DXVK content and
registry backup bindings. Profile/input payloads occur once. The frozen install
arguments remain independent of the profile's effective composed arguments.
Strict pinned C parsers cross-validate identities, arguments, input aliases and
publication expectations. SHA-256 binds the complete payload, and generated
pending/committed receipts bind that hash; committed also binds metadata bytes.
Rebuilding ProfilePublication derives parser validation from those frozen bytes.

The journal is an exclusive owned-0600 file in an existing private control
root. Creation validates external backups first. Recovery reports four explicit
states and refuses missing or changed referenced backups. The caller must retain
the publisher lease. This store does not mutate live settings, create control
roots, delete evidence, or grant ownership/readiness. A failed durable write can
leave temporary evidence and requires a fresh recovery read.

RED: missing codec entry points failed at link. Additional tests reproduced
acceptance of a nil backup UUID and a present empty DXVK file with no ownership
header; both now reject. GREEN: all 104 host tests pass. The operation test checks
all truncations and single-byte bit changes in a valid encoding, valid large
records above 1 MiB, 64 maximum-sized inputs, revision extrema, immutable
collisions, referenced-backup loss, unsafe file modes/links and real write failure
using a child-only RLIMIT_FSIZE fault. Failed journal writes remain absent for
recovery and leave private temporary evidence. Warning checks and actual SDK
v0.43 archive compilation pass; see verification.json for commands and hashes.

This is intermediate Task C work. The production callback, pending-before-live
sequencing, semantic post-write verifier, registry/DXVK retry reconciliation,
Ready/registration recovery and native interruption acceptance remain open.
Final independent Task C review and native final link are not claimed. No PS5
claim/deployment or production capability trust change occurred.
