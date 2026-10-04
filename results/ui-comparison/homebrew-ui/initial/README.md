# Direct ps5-homebrew-ui host evidence

This directory records macOS host evidence for the direct ps5-homebrew-ui
comparison candidate. It is host rendering evidence only; no console
deployment, controller-in-hand evaluation, or performance claim was made.

## Provenance

- Launcher branch: `feat/ui-homebrew`
- ps5-homebrew-ui: `8a716f294835e26a24ef5612f1535aabffef4080`
- Host API: SDL 2.32.72 and Apple's native OpenGL 4.1 core profile
- Presentation libraries: ps5-homebrew-ui kit directly; no RmlUi or RmlFocus
  library is linked

The framework source lives in the ignored isolated checkout
`build/dependencies/homebrew-ui`. `patches/homebrew-ui-macos-4.1.patch`
records every change against the pinned revision. The patch selects Apple's
core OpenGL declarations, removes GLSL 4.3 explicit uniform locations,
queries linked locations, rebases instanced attributes in place of the
OpenGL 4.2 base-instance call, replaces the OpenGL 4.3 image-copy call, and
binds a valid font atlas to every statically referenced sampler slot.

## Captures

Both files were rendered directly at the named framebuffer size and written
from that framebuffer as PNG:

| File | Dimensions | SHA-256 |
| --- | --- | --- |
| `home-1920x1080.png` | 1920 x 1080 RGBA | `e3ba3d6a43e745be2c19720798160554ab6ab20696c37249186dac351f8a2e88` |
| `home-3840x2160.png` | 3840 x 2160 RGBA | `be1e1fae0b950a651bb0d6fa2c361ebf1e716f18cf8d1856e15e525c1bdb36c7` |

The matching JSON files record the deterministic simulation snapshot.

```sh
scripts/build-homebrew.sh
build/homebrew-host/playden-homebrew --scenario default --headless \
  --capture results/ui-comparison/homebrew-ui/initial/home-1920x1080.png \
  --snapshot results/ui-comparison/homebrew-ui/initial/home-1920x1080.json \
  --width 1920 --height 1080
build/homebrew-host/playden-homebrew --scenario default --headless \
  --capture results/ui-comparison/homebrew-ui/initial/home-3840x2160.png \
  --snapshot results/ui-comparison/homebrew-ui/initial/home-3840x2160.json \
  --width 3840 --height 2160
```

`library.replay` demonstrates the replay grammar: comments and
`scenario`, `action`, `text`, and `tick` commands. `--snapshot -`
writes the resulting JSON to standard output. Fixture time advances only
through an explicit replay `tick` or the interactive host's `T` key.

## Fonts and gaps

The build bakes five shared Barlow atlases plus Noto CJK and Noto Arabic
fallback atlases. Each Barlow face skipped 151 code points requested by the
kit's European set. The CJK bake skipped 131 requested code points absent
from that face; the Arabic bake skipped 58 non-Arabic corpus code points.
The renderer selects the Noto fallbacks for CJK and Arabic titles. The kit
has no HarfBuzz/BiDi shaping: Arabic glyph coverage is present while joining,
direction, and mixed-script layout remain unsupported.
