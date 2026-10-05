# prospero-win compatibility kits

Playden pins source at `third_party/prospero-win` and packages prebuilt assets
separately. Ordinary host/PS5 launcher builds validate the exact clean source pin
and compile portable parser/lifecycle contract tests; they never build Wine,
download a compatibility runtime or create a kit automatically.

Initialize the source with `git submodule update --init third_party/prospero-win`.
The current revision is a local development commit and is **not published**.
On this Mac, initialize from the reviewed sibling repository before checking out
the gitlink if remote retrieval cannot find it:

```sh
git -c protocol.file.allow=always -c submodule.third_party/prospero-win.url="$PWD/../ps5-homebrew/prospero-win" submodule update --init third_party/prospero-win
```

The checked-in submodule URL remains the upstream HTTPS repository. Publishing
our patches requires separate authorization. Remote clean-checkout distribution
is not established until the pinned commit is reachable there or in a reviewed
fork. `dependencies.lock.json` records `published: false`; release source
validation rejects this state.

Explicit offline packaging delegates to the pinned upstream packager:

```sh
bash scripts/build-prospero-kit.sh package --spec /absolute/path/kit-spec.json --out /absolute/path/kit
bash scripts/build-prospero-kit.sh validate /absolute/path/kit
```

`PLAYDEN_PROSPERO_SOURCE_DIR` may select another **clean checkout at the same
locked revision**. A wrong revision or dirty tree fails before compilation or
packaging. `--release` on the script, or `PLAYDEN_PROSPERO_RELEASE_SOURCE=ON` in
CMake, additionally requires a published source pin. Ignored build outputs are
permitted; untracked source inputs are refused.

The spec must supply already-built title-specific wrapper/helper pairs, the
retained Wine runtime `e16ec8f`, a prepared private-state-free prefix template,
HL2 recipe, inventories and source/license provenance. The upstream packager
validates these inputs; a source pin does not certify them. No accepted kit or
kit-manifest hash is recorded yet. Keep generated runtime/template assets outside
Git. Record a verified immutable retrieval source and manifest hash in the lock
only after producer/consumer admission and fresh-bottle console acceptance.

Packaging now additionally requires `template.preparation`, identifying an
independently pinned initialization export and the approved console template:

```json
{
  "template": {
    "path": "/path/to/approved-console-template",
    "user": "prospero",
    "preparation": {
      "id": "hl2-fresh-v16",
      "export": "/path/to/fresh-v16-export"
    }
  }
}
```

Relative paths resolve against the spec directory. The Playden entry point
requires the four initialization evidence files to match independent lock pins,
checks source/loader/initialization digest links, rehashes all initialized prefix
entries, and verifies the approved console template's file hashes, sizes, modes
and directories. Missing evidence or drift fails before the upstream packager
copies assets. Direct invocation of the upstream structural packager is not this
Playden preparation-admission gate. Standalone kit validation still needs an
independently reviewed final manifest pin before production consumption.

Current local evidence is under `build/per-game-acceptance/fresh-v16-export`;
the approved template is
`build/per-game-acceptance/candidate-v16/acceptance-hl2/library/prefixes/game`.
The initialization and console template are separate immutable inputs. The
console derivation changes ten DXVK DLLs, adds the retained `wowprospero.dll`,
converts the drive symlinks to a table and changes only the CPU/audio registry
bindings. The lock certifies the reviewed host export/candidate identities;
native installation/loading/gameplay and remote artifact distribution remain open.

The experimental [Wine build](../../results/20261006-wine-build/README.md) is not a
replacement runtime input: it lacks graphics driver inputs, a complete runtime
inventory and native loading/gameplay acceptance. It does not enable additional
settings capabilities.

`prospero_kit_producer_consumer` exercises the pinned Python packager's emitted
manifest through C++ `VerifyInstalledKit`, using production streaming SHA-256.
Its synthetic PE/PRX/title fixture uses Wine's Enhanced crypto provider registry
layout in both views and repaired COM registrations. It checks independent
manifest trust, changed/extra payload rejection and a self-consistent helper
replacement against independent title pins. The fake helper allowlist override
exists only in that Python test process; production trust is unchanged. This
format/integrity regression does not establish real runtime provenance, licenses,
template initialization or PS5 filesystem/loading acceptance.
