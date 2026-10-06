# Publication registry backup store, 6 October 2026

The registry backup boundary is implemented independently of live publication.
It writes an exclusive private transaction-named file below an existing owned
0700 control directory, syncs bytes and parent, then confirms exact owned-0600
readback against its frozen SHA-256/size binding. Recovery validates the filename
against a canonical transaction UUID and binds the original registry mode.
The maximum registry backup is 32 MiB. No directory creation, orphan adoption,
cleanup or ownership/readiness grant is part of this API. Namespace stability
and coordinated writers follow `ManagedDirectory`'s documented contract; callers
must retain their publisher lease throughout these operations.

RED: the new boundary test failed because the backup API did not exist.
GREEN: the implemented API passes its host test, including binary round trip,
immutable collisions, transaction and digest drift, exact size bounds, private
mode/ownership policy, hard/symbolic links, missing files and unavailable roots.
A child-only RLIMIT_FSIZE fault forces a real write failure: the partial private
file remains and a subsequent create cannot overwrite it. Both the exact 32 MiB
limit and limit-plus-one rejection are exercised. Host warning checks and actual
SDK v0.43 archive compilation pass. See the captured verification summary.

This is an intermediate Task C implementation. The frozen operation codec,
required-backup loading, pending/committed sequencing, settings effects,
semantic verification and production/native crash retry remain outstanding.
No console claim, deployment, production trust or Wine runtime change occurred.
