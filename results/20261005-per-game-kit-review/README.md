# Per-game wrapper and kit review — 2026-10-05

Independent read-only reviews used the agent-team skill, with wrapper/lifecycle
and kit/bottle contracts reviewed separately. This is an interim review of
uncommitted upstream work, not approval of a release kit or full integration.

## Reproduced and corrected

- The title grant changed effective UID without restoring it. The shared
  `pw_data_mount_request_with` now captures, restores and verifies the original
  effective UID before checking access; it also restores after a failed helper
  exchange. Restoration failure closes startup. Host syscall substitutes model
  the observed console transition and cover success, failed request, failed
  restoration and a falsely successful restoration. The regression failed at
  the original-UID assertion before the fix. Native wrapper logs retain a
  separate credential error. Actual per-game hardware acceptance is pending.
- Playden required literal `RSA Full` as the provider name. Wine uses that as
  descriptive TypeName metadata; Type 001's Name selects the provider. Both
  admission and manifest tests rejected the actual Enhanced provider shape
  before the fix. Admission now resolves Name independently in each view and
  requires that provider's rsaenh path and type. Tests include Enhanced, a
  separate Base provider view, absent selection and duplicate Name rejection.
- Python packaging silently added executable permissions to runtime PRXs.
  It now rejects missing source execute bits instead of rewriting them. A
  regression reproduced the original acceptance and now confirms rejection
  without altering the source or publishing a destination.
- Python allowed duplicate firmware entries that Playden rejects. Packaging
  and standalone validation now reject them; the regression reproduced the gap.

## Required follow-up gates

These remain open; passing structural tests cannot certify these properties.

1. Consume and independently bind preparation's `prefix.manifest` and
   `initialization-provenance.json` during packaging and admission. The current
   packager accepts a hand-created structurally plausible prefix.
2. Bind loader initialization to the reviewed source-binding digest, retained
   runtime artifact, actual Wine source HEAD, patch/configuration identities
   and produced module hashes. A nonempty self-asserted binding string is
   insufficient. Keep retained e16 artifact identity distinct from Wine HEAD.
3. Verify DXVK DLLs against the pinned 2.6.2 archive, or an independently
   reviewed digest inventory derived from that archive; supplied PE files and
   their own supplied hashes do not establish DXVK identity.
4. Bind the complete runtime inventory to independently reviewed retained
   artifact provenance. Six essential DLLs/PRXs and a regenerated inventory
   do not establish the complete runtime.
5. Check all shipped binary payloads for host paths. Current development
   binaries contain absolute builder paths; text-only checks miss them.
6. Ship dependency license texts and bind their hashes/source identities.
7. Validate the exact Windows user registry profile/environment bindings,
   rather than only directories or unrelated substring occurrences.
8. Add a producer-to-consumer integration check using an emitted Python
   manifest and real registry shape through Playden admission. C++ fixture
   regressions cover the provider bug but are not this cross-validator gate.
9. Exercise the native wrapper glue for Home default, missing descriptor
   generic mode, invalid descriptor fail closed, credential ordering and game
   exit without catalog restart. Parser/session tests alone do not do this.
10. Prove random source, atomic session replacement, directory fsync, fresh
    private bottle, Home launch and game exit on the target console. HID module
    preload remains before grant per its existing native contract; no other
    explicit thread-producing initialization was found there.

## Verification

Playden: complete host build and 74/74 CTest tests pass; PS5 SDK build passes.
Upstream: 27 kit tests, 12 bottle preparation tests, title identity, release
packaging and native contract checks pass; both data mount tests pass.
Previously recorded portable title parser/session ASan/UBSan checks pass on
native Linux ARM64. The complete x86 suite is still unverified: Docker x86
emulation fails the original MAP32 host-execution test. No per-game console
acceptance is claimed. The user's working Playden preview remains deployed and
this review did not take a console claim or change the deployed title.
