# Playden direct ps5-homebrew-ui comparison candidate

Candidate B lives on `feat/ui-homebrew`. The presentation target uses ps5-homebrew-ui directly; it does not compile or link the launcher's RmlUi/RmlFocus presentation. Authentication, storage, downloads, profiles, runtime status and sessions are explicitly labelled **UI simulation**. No Steam services, file installation, process launching, shared-console deployment, merge or deferred performance profiling were performed.

## Inputs and isolation

| Input | Revision / location |
| --- | --- |
| Launcher comparison branch starting point | `7e1c8d201aec23739eb1d598881fd6afba49a0d5` (brief's inspected implementation baseline: `16ea24f8378e282c8bdae2637a1beef3c353d364`) |
| macOS Playden reference | `de952b3d49167e635945ada88cd8533e07d40d44`, read with `git show` from `/Users/leolobato/Documents/Projetos/Personal/GameNative/playden` |
| Direct Hui | `8a716f294835e26a24ef5612f1535aabffef4080` |
| Agent A's shared package-only commit | `2d161059540c1c73c61cfb2ffebb8a0e4efcecd1`, cherry-picked unchanged as `243ee58` |
| Kit checkout | `build/dependencies/homebrew-ui`, a detached candidate-specific worktree with [recorded patch](../../../patches/homebrew-ui-macos-4.1.patch) |
| Assets/fonts | Exact shared `fixtures/ui-comparison/` package; existing `fixtures/library.json` is unchanged |

`tools/verify_ui_comparison.py` checks the 27 documented asset hashes and byte equivalence of every shared package file against Agent A's preparation commit. Only that package was imported from `feat/ui-rmlfocus`.

## Build and run on this Mac

```sh
scripts/build-homebrew.sh
cmake --build build/homebrew-host
ctest --test-dir build/homebrew-host --output-on-failure
build/homebrew-host/playden-homebrew --profile build/comparison-interactive.json
```

The script resolves paths from its own repository root, including in this nested worktree. It creates the isolated dependency checkout, applies the recorded macOS GL 4.1/font-baker patch, bakes seven font atlases and builds the native host. It needs CMake, Ninja, Clang, pkg-config and SDL2. This host was built with AppleClang 17, CMake 3.30.3, SDL2 compatibility 2.32.72 and Apple's OpenGL 4.1 core profile on macOS 26.6.2.

Explicit local dependencies:

- `PLAYDEN_HUI_REPOSITORY` defaults to `/Users/leolobato/Documents/Projetos/Personal/GameNative/ps5-homebrew/ps5-homebrew-ui`.
- `PLAYDEN_STB_IMAGE_INCLUDE_DIR` is a CMake cache path, defaulting to `/Users/leolobato/Documents/Projetos/Personal/GameNative/rmlfocus/third_party/stb`. It supplies only pinned header-only image decode/write code; no Rml library is linked.
- `PLAYDEN_PS5_ENV` defaults to `/Users/leolobato/Documents/Projetos/Personal/GameNative/playden-ps5/scripts/with-env.sh`.
- `PLAYDEN_PS5_DEPS_DIR` defaults to `/Users/leolobato/Documents/Projetos/Personal/GameNative/rmlfocus/.deps`. This reads the existing SDK/OpenGL/compiler runtime dependencies, not Rml presentation code.

`--profile` selects an isolated comparison JSON file. Omit `--scenario` to restore it. Supplying `--scenario ID` deliberately resets the comparison state to that fixture before replay; capture generation uses a fresh profile for each output. Save failure remains visible and recoverable.

Host keyboard: arrows move; Enter = Cross; Escape = Circle; Q/E = L1/R1; PageUp/PageDown = L2/R2; O = Options; F = Triangle; X = Square; `/` opens Search. Printable keys insert text while editing. T advances the fixture clock outside text entry. Controller-only fixture controls are visible in QR and Settings. One logical tick advances transfers by the shared 64 MiB and verification uses the shared two ticks. There is no wall-clock background download advancement.

## Scenarios, replay and evidence

```sh
build/homebrew-host/playden-homebrew --scenario qr-waiting \
  --profile build/qr-comparison.json
python3 tools/capture_homebrew_comparison.py
python3 tools/capture_homebrew_supplemental.py
build/homebrew-host/playden-homebrew --scenario default \
  --profile build/navigation-comparison.json \
  --replay results/ui-comparison/homebrew-ui/replays/navigation-tour.txt \
  --record-dir build/navigation-recording \
  --snapshot results/ui-comparison/homebrew-ui/navigation-final.json
ffmpeg -y -framerate 2 -i build/navigation-recording/frame-%04d.png \
  -c:v libx264 -pix_fmt yuv420p results/ui-comparison/homebrew-ui/navigation.mp4
```

Replay lines are `action <logical-input>`, `text <UTF-8>`, `tick <count>` or `scenario <ID>`, with `#` comments. Logical inputs include normal controller names (`down`, `cross`, `circle`, `r1`, etc.) and fixture/service actions. The matching capture adapter uses semantic route/selection setup to freeze the common screenshot definitions; those setters are not controller-navigation evidence. The separate controller tour tests walk visible controls with directional input and Cross.

The [capture gallery](gallery.html) and [shared screenshot set](screenshots/) follows all 26 common capture definitions at both 1920×1080 and 3840×2160. Each has a [state assertion](assertions/), rendered-focus diagnostic sidecar and replay. Nine supplemental outputs expose Arabic, Details favorite focus, signed-out identity, Library refresh, delayed-art arrival and the stress grid. Capture validation checks native PNG dimensions, fixed fixture clock, route/surface, selected game, default active job bytes/focus and safe uninstall focus. Both sizes are rendered to their native targets; no completed 1080p image is upscaled. The top-bar reference clock is fixed at 21:42; the logical fixture clock is separately fixed at 1760000000 for matching capture state.

[Navigation recording](navigation.mp4) and [step assertions](navigation.jsonl) show one continuing session, not independent screenshot states. The recording uses two logical steps per second for review; it is not a frame-rate or performance measurement. See [coverage](coverage.md), [framework gaps](framework-gap.md), [host compatibility](host-compatibility.md), and [verification output](test-output/).

## PS5 package

```sh
scripts/build-homebrew-ps5.sh
```

The local native package is `build/homebrew-ps5/dist/FAKE77031/`, containing `eboot.bin`, `sce_sys`, `sce_module/libc.prx`, shared fixtures and baked assets. The script performs configure/build/link/sign/package only. It contains no console discovery, upload or launch action. The native entry point opens a 3840×2160 EGL surface, uses the kit's pad/input tracker, releases the held splash after the first presented frame, and closes the renderer/display on confirmed Quit.

The title attempts candidate-local persistence at `/app0/playden-hui-comparison/profile.json`; filesystem writability and physical pad/rendering behavior require coordinated console verification. A failed save produces the recovery flow rather than becoming an installation path. Fixture volume labels never produce real writable game paths.

Known rendering limitations include missing Greek accented alpha/omicron glyphs, Arabic shaping/bidirectional layout, whole-string fallback rather than shaped runs, and dim-panel blur approximation. The full per-component report distinguishes these limitations from functional simulation checks. No native 4K60, RSS, upload time, controller latency or physical-console claim is made.

Final code review/build point: `7c961f9`. Native host CTest passed 6/6; strict UndefinedBehaviorSanitizer simulation run passed. See [artifact manifest](artifact-manifest.json) for local host/package hashes and the [independent review](test-output/review.md). AddressSanitizer could not enter main on this host; its initialization boundary is recorded in [sanitizer notes](test-output/sanitizer-notes.md).
