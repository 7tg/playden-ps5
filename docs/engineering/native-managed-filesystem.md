# Managed filesystem contract for PS5

The approved integration plan defines bottle separation as configuration
isolation, not an adversarial security boundary. FW13.60 relative `openat` is
unavailable in the measured title environment; descriptor aliases do not provide
a usable anchor. `src/platform/managed_directory.{h,cpp}` therefore implements
an explicit absolute-path backend on PS5. Hosts retain descriptor operations.
No global `openat` pathname fallback or process-CWD change is introduced.

## Policies and assumptions

Callers authorize an exact absolute root and serialize cooperating writers for
an install. A game must be confirmed stopped before mutating its configuration,
cache, registry or save paths. Public ancestors above a private root are allowed.
Private roots and descendants require captured-EUID ownership and exact `0700`;
existing foreign/public directories are refused, never repaired by chmod/chown.
Approved-tree contexts permit existing asset/game ownership and permissions.

All supplied components reject traversal, symlinks, NUL and non-normalized paths.
Contexts retain directory descriptors and observed ancestor device/inode/owner/
mode identities. They validate before and after operations and refuse a
persistently renamed/replaced context. The absolute backend's descriptor is for
identity and sync, not pathname anchoring. A hostile same-UID/root writer can
still replace an ancestor inside the pathname syscall interval. Postchecks can
report failure after effects. This policy assumes stable managed namespaces.

Exclusive output creation verifies a regular, captured-EUID-owned file and
requested permissions before returning a writable descriptor. Reads reject
FIFOs/special files and remain bounded in the consuming hash/parser. Named
rename and removal validate contexts; existing directories and foreign output
files are not replaced. A checked absent destination is not an atomic no-replace
primitive against concurrent writers. Failure never cleans through a replaced
context. The caller must reconcile possible effects.

## Durability and consumers

The caller preserves file fsync, close, same-filesystem rename, then directory
fsync. Directory creation flushes both child and parent before journaled
preparation advances. Cross-parent publication must sync both parents.
`EnsurePrivateDirectories` and `HashArtifactFile` now use the shared context.
The hash still validates opened-file size, mode and timestamps before/after
streaming bytes and checks independent expected content identities at admission.

The OWNER install registry now uses narrow `Storage::WriteDurable` and
`ReadDurable` operations. An existing absolute, owned `0700` root and each
relative parent are required. Writes create a unique exclusive `0600` temporary,
sync/close the file, publish through the checked context and sync the parent.
Failures retain temporary evidence and invalidate the loaded repository even
when rename already took effect. No further reservation/update is allowed until
reload reconciles the visible journal and syncs its file/parent. Owned legacy
`0644` journals are read for migration; new publication uses `0600`.

Provision a dedicated private journal root before wiring the native installer.
Do not point it at packaged/public `/app0/playden-data`, rely on the permissive
legacy `EnsureRootDirectory`, or silently chmod an existing shared tree. Relative
storage roots are unsupported by these narrow methods. Temporary files left by
failed writes need explicit future reconciliation/cleanup; they are never
reused or followed. General Steam storage callers retain their existing APIs.

`WriteDurableExclusive` preserves immutable launch history under the documented
serialized-writer contract, checking destination absence before creating its
private temporary and again before rename. `RemoveDurable` removes only an owned
regular record and syncs the parent, including when the record is already absent.
A failed directory sync after rename/unlink is reported as uncertain evidence.
Launch history/pending/clear now use these narrow APIs. An uncertain handoff write
freezes the service instance before any repeated backend effects; reconstruction
reads and syncs the actual visible pending state. The 14-line format is retained
with bounded stream-free conversion and strict parse-before-write validation.

Settings persistence, registry overlays, DXVK publication, input reads and named
cache removal now use the same managed context. Private settings reads admit
owned legacy `0644`, while replacement and rollback write `0600`; game/DXVK
inputs use approved-tree policy. Serializer and publication/rollback fault
injection remain exercised. `PrepareGameSettingsStorage` provisions a dedicated
`game-settings-private` child beneath general state, imports the legacy file
without deletion or overwrite, and validates the chosen private record before
enabling writes. Public general state is not chmodded. Corruption of an existing
private record never falls back to stale legacy bytes. Host CLI destinations
are explicitly resolved before selecting the managed root; native/bottle paths
retain strict component validation.

The production session source maps only canonical `/data/playden/games/<install>`
roots, admits the bottle's complete owned template marker, and reads bounded
owned `0600` records from private `logs/sessions`. UUID syntax matches the pinned
wrapper's lowercase dashed hexadecimal contract, including caller-supplied IDs.
Historical runtime/settings records remain visible for individual rejection by
reconciliation. Unknown files, temporary/lock evidence and exceeded limits leave
recovery pending; no evidence is silently dropped or deleted.

`DurableReconcileJournal` uses the narrow durable storage API with one validated
session ID per filename and a strict bounded schema. Cloud-running, completion-
pending and completed records require the operation ID retained by the actual
reconciler; missing IDs are rejected on load and save. Provision its private root
separately from wrapper logs. Source/journal tests consume the actual pinned C
writer, verify restart/readback, identity drift, corruption and ownership refusal,
and exercise the explicit PS5 backend on the host. This supplies storage adapters;
Cloud/controller wiring, evidence archival and full native session lifecycle
acceptance remain open. Fresh synthetic native records passed the actual source
and journal save/reopen/pending-evidence checks under EUID 1; see
`results/20261006-managed-filesystem/session-native.log`. The final native link
exposed unsupported locale imports from string streams, so serialization now
uses escaped strings and locale-independent integer conversion.

## Evidence and remaining acceptance

Host tests exercise both backends, private policy inheritance, static symlinks,
FIFO refusal, permission/identity drift, named publication/removal and real sync.
Native-policy tests run the actual private-directory and hash consumers with
relative operations replaced by the observed `EINVAL` failure; both observed
failure before migration and pass with the explicit absolute backend. Existing
installer/hash tests remain required. Fixture temporary roots are canonicalized
on macOS so `/var` aliases do not accidentally bypass or invalidate the policy;
production supplied paths are not silently canonicalized through symlinks.

The actual installer suite also runs with all four relative directory operations
(`openat`, `mkdirat`, `fstatat`, `unlinkat`) replaced by `EINVAL`, using the PS5
backend. Its partial stage-marker write test initially failed because legacy
cleanup left an unidentifiable staging directory. Cleanup now validates the
captured directory identity, lists only its initial marker, removes that owned
0600 regular marker, syncs the child, rechecks the directory identity through
its parent, removes the empty directory and syncs the parent. Both ordinary and
native-policy suites pass. Native directory enumeration is separately verified
below; this host regression does not close installer console acceptance.

SDK compilation and default/probe-enabled final package linking are required.
The probe-enabled link originally lacked the new managed archive; its unresolved
symbol failure was reproduced and corrected in the explicit link/dependencies.
Default builds retain `PLAYDEN_TITLE_CONTRACT_PROBE=OFF`.

The actual backend and migrated directory/hash consumers passed the fresh-tree
PS5 diagnostic after the normal grant restored EUID 1. It verified directory
ownership/0700, exclusive file ownership/0600 before bytes, file and directory
fsync, write/read/hash, file/directory publication, rejection of the old renamed
context, named removal and preservation of the proof file. Evidence is in
`results/20261006-managed-filesystem/native.log`. FTP could not read the private
proof file; native readback and the production hash are the evidence. This is
successful syscall/readback evidence, not a power-loss recovery test.

Native settings migration/save/reopen/no-overwrite and legacy preservation
passed using the actual production writer/parser. Shared settings setup also
passed natively, including selecting the existing private record. Directory
enumeration (`fdopendir`/`readdir`) passed with context validation. Evidence is
in `results/20261006-managed-filesystem/{settings-native,shared-settings-native}.log`.

Full installer retry, settings reset through the live UI, registry
backup/rollback and targeted cache removal still require native acceptance. Native readiness and full installation/gameplay acceptance remain
open.

The native launch journal and isolated `SYS_flock` (131) qualification passed;
see `results/20261006-managed-filesystem/launch-lock-native.log`. Independent open
descriptions obey shared/exclusive nonblocking exclusion and close releases locks.
A production wrapper/publisher guard still needs the stable-inode protocol,
pending ownership receipt, interruption tests and cross-process native acceptance.
Do not infer that Home/settings races are solved from this primitive check.

`ManagedFileLock` admits a preexisting private `0600`, current-EUID-owned,
regular single-link inode through `ManagedDirectory`. It takes a nonblocking
shared reader or exclusive publisher lease and rechecks directory, named-file
and held-descriptor identities. Failed admission closes/releases the descriptor;
move/destruction release only the held lease. Callers create and sync the stable
file before use and never replace it. This remains a cooperating-writer contract.
Hosts use `flock`; PS5 uses the qualified `SYS_flock` gateway, with an unavailable
provider refusing acquisition. No acquisition creates or edits a file. Actual
PS5 PID1089 exercised this production adapter's multiple-reader exclusion,
last-reader release, exclusive-reader exclusion and missing-file refusal. Host
process-exit release passed; cross-process PS5 wrapper/Wine lifetime acceptance
remains pending. See the production-lock native log in the evidence directory.

The pinned prospero source `0ac92b3` provides bounded receipt/schema-2 metadata
parsers and a source-pinned scalar SHA-256 provider. Playden's
`publication_contract` writer accepts the same C types and returns bytes only
when those exact C parsers accept them. Canonical metadata binds catalog/input
hashes, the explicit DXVK presence/digest target and typed semantic registry
expectations; output is not an ownership grant. Current installer schema-1
metadata and its callback still await replacement by the journaled transaction.
The new formats are independently tested, but are not yet used to admit Wine or
expose real Play. See the managed-publication continuation plan for native guard,
interruption recovery and controller acceptance.
