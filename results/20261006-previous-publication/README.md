# Prior publication recovery proof, 6 October 2026

Review before writer wiring found that replacing the old committed receipt with
pending state would discard the evidence needed to identify remaining old
profile/input/catalog/descriptor files after a partial update. The frozen
operation now retains the prior committed receipt and exact metadata. Both are
included in its digest and cross-validated for transaction separation, phase,
previous revision, install/title/data root, metadata identities and content hash.
The producer must capture and verify the complete prior set under its publisher
lease before changing the receipt; frozen bytes alone do not grant ownership.
Revision zero still permits a fresh install, and the future writer must use its
verified preflight to distinguish that from an existing revision-zero commit.

The operation also freezes a nonnegative registry timestamp. A retry can derive
the same registry section text from its immutable original backup rather than
insert a new timestamp after every interruption. The live mutation code is not
yet connected. The wire marker changes to PWOP2 so previous development drafts
cannot be used as recovery proof. Semantic operation schema remains 1; no
production journal migration is introduced for this unpublished feature.

RED: a settings update with previous revision six and no prior ownership bytes
was accepted. GREEN: it now rejects; retained prior evidence round-trips, and
missing/altered/pending evidence, wrong prior revision and reused transactions
reject. Tests cover signed timestamp extrema/drift and old wire-marker refusal.
All 104 host tests pass, as do warning checks and the actual PS5 SDK v0.43 archive
build. No PS5 claim, app deployment, live file edit or readiness grant occurred.
Live publisher sequencing/collision admission, interrupted retries and final
Task C review/native acceptance remain open.
