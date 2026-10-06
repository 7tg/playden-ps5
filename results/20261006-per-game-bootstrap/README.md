# Per-game title bootstrap acceptance

The isolated `FAKE78001` fixture is now registered and launches as its own PS5
title. Console-local ShadowMount API accepted the owned folder registration and
scan; `/user/app` enumeration subsequently included `FAKE78001`. The successful
launch reached the wrapper's native logging and helper code. This disproves a
global ShadowMount mounting failure for this test; no reboot was required.

The wrapper was built from local `feat/playden-per-game-title` at `c1b81d3` with
the pending per-game lifecycle changes. The package truthfully records a dirty
source tree. Build foundation is `9c0b994a048521af6fb84c73ded364504fe250e9`,
SDK v0.42, using the patched title-specific Lapy helper. Wrapper eboot SHA-256:
`fd4fab3e7e0c7cffe56e0e1b95b7719bf3a0d977a40e68afba4d21b9935439cf`.

The initial fixture deliberately contains no runtime, bottle or game profile.
Its descriptor selects `acceptance-hl2-fresh-v16` under `/data/playden/games`;
the existing `PPSA99995` bottle and Playden downloads are untouched.

## Verified and remaining

`wrapper-network.log` proves HID preload and successful native grant, including
`credential_errno=0`. Session startup then refuses to create its private logs
directory with `Permission denied`. A subsequent native diagnostic confirms
that the FTP-created root and four library directories have UID 0 and mode
`0700`; the app's restored effective UID is 1. The production provisioner must
create private directories and files for the app user. FTP copying alone does
not provide that contract. Session durability and profile loading remain open.

Two temporary diagnostic attempts to transfer directory ownership crashed at
the ownership operation: first the dynamic `chown` import, then the SDK-style
direct syscall adapter. The logs locate the failure after a successful EUID
switch and directory checks. They do not prove the underlying kernel cause.
Both ownership implementations/actions were removed from the retained source.
No directory permissions were relaxed. A supported native ownership mechanism
or provisioning path must be established before the full bottle is copied.

## Retained developer tooling

The title-contract probe now runs after `PrepareNativeStartup`, before display
initialization. It uses verified stable paths and does not request the helper
again after network threads have started. The new `register-bootstrap` action
validates the fixture owner marker, launch descriptor and native title metadata
with the bounded JSON parser before requesting registration. It is restricted
to this fixed acceptance fixture; it is not production title allocation or
ownership admission. Host regressions cover mismatched identity, traversal,
duplicate keys, malformed and oversized metadata.

All 76 host tests pass. PS5 package builds pass with the probe enabled and
disabled. The disabled build remains the default configuration.

After every console attempt the normal reviewed `FAKE77031` preview was restored
and read back byte-for-byte (eboot SHA-256 `109e55a60d1e554104ba787421cce77ef2c153acc2379284cf5364af6d1552b1`).
The final error dialog was dismissed and the console claim released. The fixture
remains installed for continued acceptance; this does not mark any game ready.
