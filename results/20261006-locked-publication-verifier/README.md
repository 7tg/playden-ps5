# Shared locked publication verifier, 6 October 2026

Prospero commit `79cf85c065e49ae57679ff5babb88bd290b9e9e9` exposes
`pw_title_publication_verify_locked` through the same internal file/registry
verification used by Wine startup admission. It accepts the exact frozen pending
or committed receipt and metadata digest while the caller retains its qualified
exclusive lease on the stable lock. It validates that borrowed descriptor's
identity, does not acquire/convert/release/close it, and returns cached contents
without a reader/startup lease. Caller must hold the lease through all effects
and independently prove stopped-title activity. Output remains unchanged after
a refusal. Normal reader admission still requires committed state and its own
qualified shared lease.

RED: the publisher verification contract failed at link before the API existed.
GREEN: optimized and UBSan native guard tests pass, including both receipt phases,
exact receipt/metadata/input drift, foreign descriptors, output preservation,
absent shared callbacks and continued reader exclusion after successful/failing
verification. The preexisting reader/registry/DXVK refusal suite runs against the
same shared implementation. Portable publication and scalar SHA tests pass.
The complete parent host suite passes 104 tests at the new clean submodule pin.

Actual foundation `9c0b994` SDK v0.42 object compilation passes under warnings-as-
errors. A clean exact FAKE78001 wrapper with its patched title helper also builds
and packages, with source_dirty=false, required descriptor and the independent
scalar-provider provenance marker. Its native build manifest is copied here.
The plain LLVM imports and required module sets match the prior clean wrapper:
136 imports, 11 modules, no additions/removals. Encoded NID library/module indices
changed with table order; the import evidence records both forms. The first
build invocation lacked PS5_CLANG; retry used the explicit Clang 18 executable.
No dependency preparation or Wine runtime rebuild occurred.

Playden updates its development source pin and diagnostic-source identity to this
commit. The retained tested Wine artifact remains e16ec8f and production guard
capability trust remains disabled. The owned upstream worktree's unrelated
untracked probe was preserved; the clean pinned submodule supplied the final
wrapper build. Neither repo was pushed. No PS5 claim or deployment occurred.

This is the shared verifier needed for Task C's publisher, not native acceptance
of the new source or live publication. The prior native Playden admission result
used bde4b394. New-source publisher interruption/retry, concurrent-process
contention and actual Wine-wrapper lifetime/gameplay remain pending.
