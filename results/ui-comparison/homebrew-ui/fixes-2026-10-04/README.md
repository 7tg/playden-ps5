# Fixes after selecting ps5-homebrew-ui

The direct candidate was merged into `main` as `3bc1e38`, retaining the Steam
backend work. The original comparison captures and manifest remain historical
records; this directory contains native captures of the subsequent fixes.

- Confirmation headings wrap inside the panel. Body placement follows the
  wrapped heading, with line limits keeping it clear of the buttons.
- Active root tabs use an opaque orange fill and dark text.
- The CFF baker patch restores cubic outlines in Japanese/CJK text. A licensed
  supplemental Noto Sans fallback supplies every character of the Greek fixture
  title, including its accented letters. The original fixture package is unchanged.
- Home and Library posters scale with a per-visible-key spring. Constants are
  copied from RmlFocus `theme/themes/ember.rcss` at `31d2d8f`: **1.08** poster scale,
  **0.24 s** response, **0.62** damping. The response conversion and **1/480 s**
  integration steps match `include/rmlfocus/spring.h`. Interrupted moves preserve
  velocity. Offscreen keys are pruned; reduced motion snaps to the target.
- Interactive host and PS5 presentation use monotonic elapsed time. Replays use
  deterministic 1/60 s presentation steps without advancing the simulation clock.

Arabic shaping/BiDi remains deferred. These fixes do not add Rml presentation
linkage, real installation/network/launch behavior, or performance profiling.

## Reproduce

From the main checkout:

```sh
scripts/build-homebrew.sh
cmake --build build/homebrew-host
ctest --test-dir build/homebrew-host --output-on-failure
python3 tools/capture_homebrew_fixes.py
scripts/build-homebrew-ps5.sh
```

The capture tool requires Pillow and ffmpeg. It renders eight native framebuffers
at 1920×1080 and 3840×2160 and verifies the quit heading does not cross the dialog
edge and the Home tab is orange. Its 128-frame, 60 Hz focus recording includes a
rapid retarget; the fixture clock stays at 1760000000. `frame N` advances only
presentation while recording; `--record-motion` records individual frames instead
of settled states. Atlas tests compare silhouette coverage with the source font
rasterizer and require every codepoint of the Greek fixture title.

Physical PS5 verification of the new package remains pending. No console upload
or deployment was performed. Local build artifacts remain under
`build/homebrew-ps5/dist/FAKE77031/`.

The pre-existing six library UI WIP files are preserved byte-for-byte in stash
`1f8f649d6e1430f1898b942845db53db668f17a4`, named
“Pre-existing library UI WIP before homebrew fixes”.

## Results

Verified source commit: `3796598`. Host CTest **11/11**, existing CTest **14/14**,
eight native capture checks and the 128-frame focus recording all passed. The
local native package rebuilt successfully. See [manifest](manifest.json),
[host tests](host-tests.log), [existing tests](existing-tests.log),
[visual checks](visual-tests.log), and [independent review](review.md).

| Surface | 1080p | 4K |
| --- | --- | --- |
| Quit heading | [Capture](quit-wrap-1920x1080.png) | [Capture](quit-wrap-3840x2160.png) |
| CJK Library label | [Capture](cjk-library-1920x1080.png) | [Capture](cjk-library-3840x2160.png) |
| Greek Details title | [Capture](greek-details-1920x1080.png) | [Capture](greek-details-3840x2160.png) |
| Home selected tab | [Capture](home-tabs-1920x1080.png) | [Capture](home-tabs-3840x2160.png) |

[Focus scale recording](focus-motion.mp4) — frame-by-frame presentation at
60 Hz, including an interrupted move; no runtime frame-rate claim.
