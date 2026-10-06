# Native publication acceptance diagnostic

This developer-only payload tests the exact pinned native guard and shared-lock
callback on real canonical files. Separate payload processes contend on one
stable lock; it tests return, `_exit`, SIGKILL and a durable pending receipt left
by a terminated publisher. It creates a fresh unregistered `FAKE78997` fixture
and a nonce-named private game directory. It uses no Wine or existing game data.

Build with the reviewed foundation checkout and a fresh output directory:

```sh
python3 tools/build_publication_acceptance.py \
  --foundation ../playden-ps5/.deps/prospero-foundation \
  --output build/publication-acceptance-reviewed
```

The build verifies the exact prospero commit, provider pin, v0.42 SDK archive and
target headers/libraries. It records actual authored source, compiler/sysroot and
ELF hashes. Review the frozen source and manifest before running it. Pass that
manifest's independently recorded SHA-256 through
`--reviewed-manifest-sha256`; the runner refuses changed source/SDK/artifact bytes.
A failed build does not replace any previous build directory.

Claim and inspect the console before running the diagnostic. Use the console's
actual elfldr port (9021; port 9305 is the controller payload):

```sh
python3 tools/run_publication_acceptance.py \
  --manifest build/publication-acceptance-reviewed/manifest.json \
  --reviewed-manifest-sha256 REVIEWED_MANIFEST_SHA256 \
  --foundation ../playden-ps5/.deps/prospero-foundation \
  --host 10.0.1.68 --port 9021 --console-claimed \
  --output results/UNIQUE_NATIVE_ACCEPTANCE_RUN
```

The runner preserves a nonce/PID-bound transcript and JSON result. Successful
cleanup requires publisher ownership and exact marker/inventory checks. If
holders cannot be shown to have released the lock, cleanup is skipped and the
fixture remains for diagnosis. Collect evidence, then immediately release the
console. No reboot, title registration, app replacement or service restart is
part of this diagnostic.

A passing result qualifies standalone payload-context guard/lock behavior. It
does not qualify the Lapy grant, `/app0` loading, `wine64_main` lease retention,
Wine process behavior or native title exit. Those require the separate actual
wrapper acceptance before production trust can admit guarded runtime kits.


## Current console result

The 6 October standalone attempt **did not pass**. It created an owned UID1
fixture, but the fixed native-title libkernel handle does not resolve from this
payload context; the production guard refuses startup with an unavailable
provider. Native Playden-context admission and identity-checked cleanup subsequently passed;
actual Wine wrapper and concurrent-process qualification remain pending.
Do not substitute a payload-only gateway and claim full wrapper acceptance.
See [recorded evidence](../../results/20261006-managed-publication/README.md).


## Native Playden bridge

A `PLAYDEN_TITLE_CONTRACT_PROBE=ON` build links the same pinned guard and fixture
implementation into the native Playden title, after its Lapy grant. The default
production build excludes these diagnostic objects. The request file accepts:

```text
publication <16 lowercase hex nonce> reader-try
publication <16 lowercase hex nonce> reader-hold
publication <16 lowercase hex nonce> writer-try
publication <16 lowercase hex nonce> cleanup
```

The bridge accepts existing, exact nonce-owned fixtures only. It does not create
fixtures, change UID, register titles or start Wine. `reader-hold` deliberately
leaves the acquired lease open while the caller parks the title; terminate that
title to release it. Do not treat an uncontended check as cross-process proof.
Back up the actual current eboot and package identity before temporary diagnostic
deployment, restore those same bytes with independent readback, then release the
console. See [native result](../../results/20261006-publication-native-title/README.md).
