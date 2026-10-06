# Managed publication portable boundary

The continuation plan is
[2026-10-06-managed-publication.md](../../docs/superpowers/plans/2026-10-06-managed-publication.md).

Verified development source: `0ac92b3c010fb3d992f9e75322ef53cc97284a7b`,
including scalar crypto provider commit `6571f64`. Both the owned prospero
worktree and Playden's pinned submodule are clean. No push or published release
is claimed; the retained tested Wine runtime remains `e16ec8f`.

The portable receipt parser requires exactly 13 schema-1 fields; the schema-2
metadata parser requires 18 base fields and the bounded indexed input/registry
fields. Tests exercise uint64 extrema/overflow, duplicate/unknown fields and
escaped JSON key aliases, output preservation on rejection, canonical path/UUID,
64-input/2-registry bounds, case aliases, hex lengths, canonical empty fields,
DXVK presence/absence, and typed registry expectations. C++ callers link against
C-compiled parser objects. The C++ writer emits bytes only after those same
parsers accept them; it bounds fixed C strings before reading, preserves exact
uint64 values and uses no locale-dependent streams.

Observed RED: missing parser entry points at link; missing parent writer header.
GREEN: focused strict C/C++17 tests and independent UBSan parser build/run.
Playden full host suite: **100/100 passed** after the exact source-pin update.
Independent reviews found no actionable issues in parser, writer or crypto.
Full upstream `make test` is not acceptance: this macOS ARM environment encounters
preexisting x86 hostexec and diagnostics build limitations. Optional macOS ASan
stalls before main; sanitizer success is not claimed for the SHA provider.

SHA provider sources are byte-identical selected files from the independently
verified mbedTLS 3.6.6 archive `8fb65fae8dcae5840f793c0a334860a411f884cc537ea290ce1c52bb64ca007a`.
Its provenance inventory binds selected headers/license, scalar configuration and
namespaced authored wrapper/zeroizer. Independent known-vector/streaming tests
include empty, `abc`, padding boundary, multiblock and million-`a`; invalid state,
null arguments, bit-length overflow and zeroization are covered. The worker's
actual SDK v0.42/foundation `9c0b994` compile and combined relocatable link have
only `memcpy`/`memset` unresolved; optimized volatile zero stores were inspected.
These are object checks, not a native wrapper run or file-admission proof.

Actual private-lock acceptance is recorded separately in
[production-lock-native.log](../20261006-managed-filesystem/production-lock-native.log).
The PS5 normal preview was restored and the console released. The native guard,
semantic registry/DXVK file checks, journaled publisher, crash retry, real Play
controller, gameplay/save/Cloud/maintenance acceptance remain outstanding.

Playden SDK production contract archive and default diagnostic-OFF package build
passed. The package remains exactly the restored `d76d1a...` preview: the new
contract library is not yet called by the production UI or linked admission path.
The C++ serializer object also compiled directly with the actual SDK compiler.
