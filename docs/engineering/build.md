# Building Playden Launcher

Playden uses ps5-homebrew-ui directly and builds with renderer-independent library query/cache, storage, Steam service, reduced protobuf codec, QR authentication client, and verified HTTP transport targets. Setup (submodules and `scripts/fetch-deps.sh`) is in [CONTRIBUTING.md](../../CONTRIBUTING.md). The legacy RmlFocus presentation was removed on 2026-10-06.

## macOS host

Check the pinned ps5-homebrew-ui submodule, bake the pinned fonts, configure, and build:

```sh
scripts/build-homebrew.sh
cmake --build --preset homebrew-host
ctest --preset homebrew-host --output-on-failure
build/homebrew-host/playden-homebrew
```

The host starts in live-service mode and stores the private Steam session under
`build/homebrew-host/playden-data` by default. Use `--data-dir PATH` to select another live
storage root. Relative host paths are resolved to absolute paths; the trusted
CLI destination's existing aliases are resolved before storage is selected.
Game settings use an app-created `game-settings-private` child with 0700
permissions and 0600 files. Owned legacy `game-settings.json` is imported once
without deletion; an existing private record takes precedence and corruption
blocks automatic writes. The general storage root keeps its existing mode.
Fixture profiles, replays, captures, snapshots, recordings, and headless runs are
available only in an explicit simulation context, for example:

```sh
build/homebrew-host/playden-homebrew --simulation --scenario signed-out
build/homebrew-host/playden-homebrew --scenario qr-waiting --capture build/homebrew-host/qr.png
```

`--profile` configures simulation presentation state; it is never read as a credential file.
State-dump options are rejected in live mode so an active QR challenge or account identity cannot
enter generic comparison evidence.

The selected test preset includes the Hui behavior checks, live model/controller/session/redaction/QR
checks, plus `library_query`, `library_cache`, `proto_wire`, `auth_messages`, `steam_json`,
`token_claims`, `http_client`, `auth_client`, `session_store`, and `steam_auth`. The HTTP check uses a
local TLS fixture with its own test CA; it does not contact Steam. Runtime trust is separate:
`build/homebrew-host/trust/cacert.pem` is copied from the pinned curl 8.22/Mbed TLS dependency
prefix. The vendored Nayuki QR C sources and MIT license are pinned by exact hash in
`dependencies.lock.json`.

The build rejects a ps5-homebrew-ui revision mismatch or local changes in that submodule, modified font/license inputs, a modified `stb_image.h`, and mismatched curl headers, transport archives, or CA bundle. If the pinned HTTP dependencies are absent, `scripts/build-homebrew.sh` prepares them from source archives whose hashes are recorded in `dependencies.lock.json`.

The direct UI constructs the verified HTTP authentication backend, lazy private session repository,
and asynchronous service controller only in live mode. Service work is drained before drawing and
is canceled and joined before graphics teardown. Owned-library retrieval remains a later stage, so
a successful sign-in does not synthesize an empty or fixture library.

## PS5 build-only package

```sh
scripts/build-homebrew-ps5.sh
```

Build the Lapy helper first with `scripts/build-lapy-helper.sh`. The PS5 script then verifies the pinned ps5-homebrew-ui submodule and refreshes every font atlas from its pinned baker, font files, and current catalog. It then configures the selected cross-build with the allocated `FAKE77031` title ID, links the same backend foundations against the pinned PS5 curl 8.22/Mbed TLS archives, copies `trust/cacert.pem` into the title, and writes `build/homebrew-ps5/dist/FAKE77031`. `PLAYDEN_HUI_SOURCE_DIR` selects another clean checkout at the pinned revision. The toolchain comes from `deps/` through `scripts/with-env.sh`; `PLAYDEN_PS5_ENV` and `PLAYDEN_PS5_DEPS_DIR` override them. The script does not discover, upload to, launch, or otherwise contact a console. A successful package is build evidence only.

Normal packages start live, use `/app0/playden-data` through the worker-owned session repository,
and do not contain scenario, profile, replay, or synthetic-library files. A developer-only
simulation package is explicit:

```sh
PLAYDEN_UI_SIMULATION=ON scripts/build-homebrew-ps5.sh
```

That switch packages the comparison fixtures and starts the same presentation in labelled
simulation mode. It does not change the default package or contact a console.

The PS5 custom link depends on every backend archive and pinned curl/TLS archive, and packaging depends directly on the CA bundle. Changes therefore relink or restage the title without requiring a clean build.

## Artifacts and evidence

- Host executable: `build/homebrew-host/playden-homebrew`
- Host tests and trust: `build/homebrew-host/*_test`, `build/homebrew-host/trust/cacert.pem`
- PS5 title and executable: `build/homebrew-ps5/dist/FAKE77031`, including `eboot.bin` and `trust/cacert.pem`
- PS5 runtime log after a console run: `/app0/logs/playden.log`

Console launch, navigation, clean exit, storage, and frame-delivery results remain separate runtime evidence.

## Recorded console validation

The milestone 1 [shell run](../../results/20261004T132145Z-shell/README.md) verified `/app0/playden-data` atomic writes and preference restoration on this console, 3840×2160 EGL/render dimensions with an accepted 60 Hz mode, controller navigation through MCP, and clean exits. This does not establish other firmware support, power-loss durability, physical DualSense feel, or the milestone 2 sustained performance gate. The runtime probe remains enabled so storage failures are visible on different environments.
