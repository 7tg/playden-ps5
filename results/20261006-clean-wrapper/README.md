# Clean per-game wrapper build

The FAKE78001 wrapper built successfully from pinned clean source
`df261e9469b0faab7ef145441bb192f762472524`, using the already prepared
foundation `9c0b994a048521af6fb84c73ded364504fe250e9` (SDK v0.42).
Dependencies were verified and reused rather than rebuilt. The explicit patched
helper and manifest were supplied; no generic helper download was used.
The emitted build manifest records `source_dirty: false` and the real eboot hash.
[Inventory](inventory.json) records every output file's size, mode and SHA-256;
[dynamic imports](dynamic-imports.txt) records the final PIE imports.

The producer's `_validate_no_personal_state` passed for the native title folder.
No dev.conf was copied. The same scan of the retained runtime input refused
`lib/wine/x86_64-unix/opengl32.prx` because it contains a host personal path.
This confirms a remaining runtime distribution gate, not a waiver of it.
The native title was not deployed, no accepted kit was emitted, and the retained
runtime was not replaced by the experimental Wine build.

Local wrapper output: `third_party/prospero-win/dist/FAKE78001-playden-clean-kit`.
Build log: `/tmp/playden-clean-wrapper-build.log`.
