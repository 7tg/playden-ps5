# Building Playden Launcher

The launcher consumes the installed public `RmlFocus` CMake package and compiles the framework's patched RmlUi GL3 renderer into each graphical front end. `dependencies.lock.json` pins the exact RmlFocus checkout, RmlUi, FreeType, PS5 payload SDK, ps5-opengl, native-title tooling, compiler runtime, and packaged font sources. Configure refuses a different or tracked-dirty RmlFocus checkout and rejects mismatched pinned source checkouts.

The default framework source is the sibling `../rmlfocus`. Override it explicitly when the checkout lives elsewhere:

```sh
cmake --preset host -DPLAYDEN_RMLFOCUS_SOURCE_DIR=/absolute/path/to/rmlfocus
```

The override must resolve to the revision in `dependencies.lock.json`. The dependency build installs RmlFocus under the launcher build directory; it does not link the framework's in-tree CMake target. SDL2 compatibility, FreeType, OpenGL, libcurl, CMake, Ninja, and Apple Clang are host system packages. The versions used for the recorded host build are listed in the lock file.

## macOS

Install CMake, Ninja, FreeType, SDL2 compatibility, and libcurl, then run:

```sh
cmake --preset host
cmake --build --preset host
ctest --preset host --output-on-failure
build/host/playden-launcher
```

The executable and copied runtime assets are in `build/host/`. Keyboard arrows move focus, Return selects, Escape goes back, and Q/E switch the primary section; compatible controllers use the RmlFocus SDL mapping. A bounded smoke run and a real GL capture are available for build validation:

```sh
build/host/playden-launcher --run-for-seconds 2 --shot build/host/playden.ppm
build/host/playden-launcher --keys R1,R1 --run-for-seconds 2 --shot build/host/settings.ppm
```

Host preferences use the directory returned by SDL's platform preference-path API. Diagnostics are emitted as JSON lines on stderr with frame-start intervals, measured drawable/render-target dimensions, the queried display refresh mode, process resident memory, and launcher/framework revisions.

## PS5 build-only package

The PS5 build uses the payload SDK environment wrapper in sibling `../playden-ps5`. A title ID is mandatory; there is no framework, runtime, or launcher default:

```sh
scripts/build-ps5.sh --title-id FAKE77031
```

The script validates the ID, cross-compiles the installed public RmlFocus package and launcher, preserves ps5-opengl's linker script and symbol map, allocation wrappers, AGC stubs, 256 MiB heap patch, compiler-rt builtins, custom GL loader, and patched renderer, then converts and signs the local title. It only writes `build/ps5/dist/<TITLE_ID>` and never uploads, launches, discovers, or contacts a console.

The title requests a 3840×2160 surface and 60 Hz before EGL initialization. It reports surface dimensions queried from EGL and refresh only after `eglGetDisplayModePS5` reports the accepted presentation mode. PS5 memory records measure ps5-opengl's owned application heap and label that domain explicitly; they are not total process RSS. Holding Options for two seconds shuts down RmlUi/EGL and exits through `sceSystemServiceLoadExec("exit")`.

PS5 persistence is enabled only when a launch-time atomic write/read probe succeeds under `/app0/playden-data`; otherwise the application receives an unavailable storage adapter and shows a diagnostic. A successful package build is not evidence that this path is writable, that the title launched, or that native 4K60 was achieved. Record those facts separately from an actual console run.

## Artifacts and evidence

- Host executable: `build/host/playden-launcher`
- Host tests: `build/host/*_test`
- PS5 title directory: `build/ps5/dist/<TITLE_ID>`
- PS5 executable: `build/ps5/dist/<TITLE_ID>/eboot.bin`
- PS5 runtime log after a console run: `/app0/logs/playden.log`

Build metadata contains the launcher Git revision with a `+dirty` marker when tracked or untracked files differ, the pinned RmlFocus revision, and the platform environment label. Console launch, navigation, clean exit, storage, and frame-delivery results remain separate runtime evidence.

## Recorded console validation

The milestone 1 [shell run](../../results/20261004T132145Z-shell/README.md) verified `/app0/playden-data` atomic writes and preference restoration on this console, 3840×2160 EGL/render dimensions with an accepted 60 Hz mode, controller navigation through MCP, and clean exits. This does not establish other firmware support, power-loss durability, physical DualSense feel, or the milestone 2 sustained performance gate. The runtime probe remains enabled so storage failures are visible on different environments.
