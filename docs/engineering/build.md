# Building Playden Launcher

The selected consumer uses direct ps5-homebrew-ui and builds with renderer-independent library query/cache, storage, Steam service, reduced protobuf codec, QR authentication client, and verified HTTP transport targets. The old RmlFocus launcher remains available through presets whose names begin with `legacy-`; its successful build is historical regression evidence rather than selected-consumer validation.

## Selected macOS consumer

Prepare the pinned isolated Hui checkout, apply the two verified patches, bake the pinned fonts, configure, and build:

```sh
scripts/build-homebrew.sh
cmake --build --preset homebrew-host
ctest --preset homebrew-host --output-on-failure
build/homebrew-host/playden-homebrew
```

The host starts in live-service mode and stores the private Steam session under
`build/homebrew-host/playden-data` by default. Use `--data-dir PATH` to select another live
storage root. Fixture profiles, replays, captures, snapshots, recordings, and headless runs are
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

The selected build rejects a Hui revision mismatch, unexpected files in its isolated patched checkout, modified patch files, modified font/license inputs, a modified `stb_image.h`, and mismatched curl headers, transport archives, or CA bundle. If the pinned HTTP dependencies are absent, `scripts/build-homebrew.sh` prepares them from source archives whose hashes are recorded in `dependencies.lock.json`.

The direct UI constructs the verified HTTP authentication backend, lazy private session repository,
and asynchronous service controller only in live mode. Service work is drained before drawing and
is canceled and joined before graphics teardown. Owned-library retrieval remains a later stage, so
a successful sign-in does not synthesize an empty or fixture library.

## Selected PS5 build-only package

```sh
scripts/build-homebrew-ps5.sh
```

This first verifies the exact prepared Hui checkout and refreshes every font atlas from its pinned baker, font files, and current catalog. It then configures the selected cross-build with the allocated `FAKE77031` title ID, links the same backend foundations against the pinned PS5 curl 8.22/Mbed TLS archives, copies `trust/cacert.pem` into the title, and writes `build/homebrew-ps5/dist/FAKE77031`. `PLAYDEN_HUI_SOURCE_DIR` continues to select an alternate prepared checkout at the pinned revision; the font refresh validates it without applying patches or otherwise modifying it. The script does not discover, upload to, launch, or otherwise contact a console. A successful package is build evidence only.

Normal packages start live, use `/app0/playden-data` through the worker-owned session repository,
and do not contain scenario, profile, replay, or synthetic-library files. A developer-only
simulation package is explicit:

```sh
PLAYDEN_UI_SIMULATION=ON scripts/build-homebrew-ps5.sh
```

That switch packages the comparison fixtures and starts the same presentation in labelled
simulation mode. It does not change the default package or contact a console.

The PS5 custom link depends on every backend archive and pinned curl/TLS archive, and packaging depends directly on the CA bundle. Changes therefore relink or restage the title without requiring a clean build.

## Historical RmlFocus consumer

The launcher consumes the installed public `RmlFocus` CMake package and compiles the framework's patched RmlUi GL3 renderer into each graphical front end. `dependencies.lock.json` pins the exact RmlFocus checkout, RmlUi, FreeType, PS5 payload SDK, ps5-opengl, native-title tooling, compiler runtime, and packaged font sources. Configure refuses a different or tracked-dirty RmlFocus checkout and rejects mismatched pinned source checkouts.

The default framework source is the sibling `../rmlfocus`. Override it explicitly when the checkout lives elsewhere:

```sh
cmake --preset legacy-host -DPLAYDEN_RMLFOCUS_SOURCE_DIR=/absolute/path/to/rmlfocus
```

The override must resolve to the revision in `dependencies.lock.json`. The dependency build installs RmlFocus under the launcher build directory; it does not link the framework's in-tree CMake target. SDL2 compatibility, FreeType, OpenGL, libcurl, CMake, Ninja, and Apple Clang are host system packages. The versions used for the recorded host build are listed in the lock file.

### macOS

Install CMake, Ninja, FreeType, SDL2 compatibility, and libcurl, then run:

```sh
cmake --preset legacy-host
cmake --build --preset legacy-host
ctest --preset legacy-host --output-on-failure
build/host/playden-launcher
```

The executable and copied runtime assets are in `build/host/`. Keyboard arrows move focus, Return selects, Escape goes back, and Q/E switch the primary section; compatible controllers use the RmlFocus SDL mapping. A bounded smoke run and a real GL capture are available for build validation:

```sh
build/host/playden-launcher --run-for-seconds 2 --shot build/host/playden.ppm
build/host/playden-launcher --keys R1,R1 --run-for-seconds 2 --shot build/host/settings.ppm
```

Host preferences use the directory returned by SDL's platform preference-path API. Diagnostics are emitted as JSON lines on stderr with frame-start intervals, measured drawable/render-target dimensions, the queried display refresh mode, process resident memory, and launcher/framework revisions.

### PS5 build-only package

The PS5 build uses the payload SDK environment wrapper in sibling `../playden-ps5`. A title ID is mandatory; there is no framework, runtime, or launcher default:

```sh
scripts/build-ps5.sh --title-id FAKE77031
```

The script validates the ID, cross-compiles the installed public RmlFocus package and launcher, preserves ps5-opengl's linker script and symbol map, allocation wrappers, AGC stubs, 256 MiB heap patch, compiler-rt builtins, custom GL loader, and patched renderer, then converts and signs the local title. It only writes `build/ps5/dist/<TITLE_ID>` and never uploads, launches, discovers, or contacts a console.

The title requests a 3840×2160 surface and 60 Hz before EGL initialization. It reports surface dimensions queried from EGL and refresh only after `eglGetDisplayModePS5` reports the accepted presentation mode. PS5 memory records measure ps5-opengl's owned application heap and label that domain explicitly; they are not total process RSS. Holding Options for two seconds shuts down RmlUi/EGL and exits through `sceSystemServiceLoadExec("exit")`.

PS5 persistence is enabled only when a launch-time atomic write/read probe succeeds under `/app0/playden-data`; otherwise the application receives an unavailable storage adapter and shows a diagnostic. A successful package build is not evidence that this path is writable, that the title launched, or that native 4K60 was achieved. Record those facts separately from an actual console run.

## Artifacts and evidence

- Selected host executable: `build/homebrew-host/playden-homebrew`
- Selected host tests and trust: `build/homebrew-host/*_test`, `build/homebrew-host/trust/cacert.pem`
- Selected PS5 title and executable: `build/homebrew-ps5/dist/FAKE77031`, including `eboot.bin` and `trust/cacert.pem`
- Historical host executable and tests: `build/host/playden-launcher`, `build/host/*_test`
- Historical PS5 title directory: `build/ps5/dist/<TITLE_ID>`
- PS5 runtime log after a console run: `/app0/logs/playden.log`

Build metadata contains the launcher Git revision with a `+dirty` marker when tracked or untracked files differ, the pinned RmlFocus revision, and the platform environment label. Console launch, navigation, clean exit, storage, and frame-delivery results remain separate runtime evidence.

## Recorded console validation

The milestone 1 [shell run](../../results/20261004T132145Z-shell/README.md) verified `/app0/playden-data` atomic writes and preference restoration on this console, 3840×2160 EGL/render dimensions with an accepted 60 Hz mode, controller navigation through MCP, and clean exits. This does not establish other firmware support, power-loss durability, physical DualSense feel, or the milestone 2 sustained performance gate. The runtime probe remains enabled so storage failures are visible on different environments.
