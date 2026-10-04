# macOS host compatibility

The direct candidate compiles the ps5-homebrew-ui kit from the pinned
`8a716f294835e26a24ef5612f1535aabffef4080` revision in the ignored,
isolated `build/dependencies/homebrew-ui` worktree. It links SDL 2.32.72
and Apple's native OpenGL 4.1 framework. It does not link RmlUi or RmlFocus
presentation libraries.

The reproducible framework delta is
`patches/homebrew-ui-macos-4.1.patch` (SHA-256
`8f672e19009e1e63601c4276979aaa31d19b7a5ff4b00184206b89041af66c09`).
It:

- selects `OpenGL/gl3.h` on Apple while preserving `GL/glcorearb.h`
  elsewhere;
- uses GLSL `#version 410 core`, linked uniform lookups, and rebased
  instanced attributes in place of OpenGL 4.2/4.3-only APIs;
- copies canvas regions through the OpenGL 3 framebuffer/texture path; and
- binds a real loaded atlas to every font sampler referenced by the shader.

The last item fixes an Apple driver diagnostic for an unloadable texture on
font unit 5. Rebuilding and capturing after the fix produces no driver
diagnostic and `glGetError` remains `GL_NO_ERROR`.

## Build and verification commands

```sh
scripts/build-homebrew.sh --prepare-only
scripts/build-homebrew.sh
cmake --build build/homebrew-host --target homebrew_simulation_test
ctest --test-dir build/homebrew-host --output-on-failure

build/homebrew-host/playden-homebrew --scenario default --headless \
  --capture results/ui-comparison/homebrew-ui/initial/home-1920x1080.png \
  --snapshot results/ui-comparison/homebrew-ui/initial/home-1920x1080.json \
  --width 1920 --height 1080
build/homebrew-host/playden-homebrew --scenario default --headless \
  --capture results/ui-comparison/homebrew-ui/initial/home-3840x2160.png \
  --snapshot results/ui-comparison/homebrew-ui/initial/home-3840x2160.json \
  --width 3840 --height 2160
```

The two smoke captures are native 1920 x 1080 and 3840 x 2160 RGBA PNGs.
Their SHA-256 values are:

- `e3ba3d6a43e745be2c19720798160554ab6ab20696c37249186dac351f8a2e88`
  (1920 x 1080)
- `be1e1fae0b950a651bb0d6fa2c361ebf1e716f18cf8d1856e15e525c1bdb36c7`
  (3840 x 2160)

These are macOS host results. They establish buildability, deterministic
replay, framebuffer capture, and native output dimensions. They do not
establish PS5 rendering, controller feel, memory use, or sustained frame
rate.

## Font bake

The build creates five Barlow atlases in
`build/homebrew-host/homebrew-fonts`, plus corpus-driven Noto CJK and
Arabic fallback atlases. Each Barlow face skipped 151 requested code points.
The CJK face skipped 131 requested code points outside its coverage, and the
Arabic face skipped 58 non-Arabic corpus code points. The fallback atlases
cover the shared CJK and Arabic fixture titles, but Hui provides neither
Arabic shaping nor bidirectional layout; Arabic remains unjoined and
left-to-right.

The final verification records six passing CTests, strict UBSan simulation, 52 common captures plus nine supplemental outputs and the independent review in `test-output/`. `initial/` contains the earlier smoke render; final comparison images and render diagnostics are in `screenshots/`. The current host/package hashes are recorded in `artifact-manifest.json`. Greek accented alpha/omicron U+03AC/U+03CC are absent in the frozen fonts; `test-output/font-coverage.log` records the glyph probes.
