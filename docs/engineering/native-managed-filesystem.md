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

The OWNER install registry still uses `Storage::WriteAtomic`, which has no file
or directory fsync. A narrow durable repository write is required before native
journal-before-effects acceptance. Settings currently use their older relative
walk and `0644` default outputs; they need a deliberate policy migration,
retaining serializer/publication fault injection and private ownership rules.

## Evidence and remaining acceptance

Host tests exercise both backends, private policy inheritance, static symlinks,
FIFO refusal, permission/identity drift, named publication/removal and real sync.
Native-policy tests run the actual private-directory and hash consumers with
relative operations replaced by the observed `EINVAL` failure; both observed
failure before migration and pass with the explicit absolute backend. Existing
installer/hash tests remain required. Fixture temporary roots are canonicalized
on macOS so `/var` aliases do not accidentally bypass or invalidate the policy;
production supplied paths are not silently canonicalized through symlinks.

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

Installer retry, settings save/reopen/reset, registry backup/rollback and
targeted cache removal still require native acceptance after their consumers
are migrated. Native readiness and full installation/gameplay acceptance remain
open.
