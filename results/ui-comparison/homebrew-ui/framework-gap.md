# Direct ps5-homebrew-ui presentation notes

## Kit capabilities used

The candidate uses the pinned ps5-homebrew-ui renderer directly. `TabBar` draws the primary
navigation, `GridView` owns the six-column library layout and focus scale, `ListView` draws
bounded settings and download rows, `ProgressBar` draws per-job progress, and `Keyboard` draws
controller text entry. Stateless `Painter` controls are used for buttons, fields, rows and
progress. The kit's shape-based `draw_hints` supplies portable DualSense face and shoulder
glyphs. Shared PNG fixture bytes are decoded with pinned `stb_image` and uploaded through
`GlBatch::create_texture`.

The 1920 x 1080 scene is submitted to the kit renderer at the requested framebuffer size. A
3840 x 2160 capture therefore rasterizes geometry and SDF text at the native target size rather
than scaling a completed 1080p framebuffer.

## Custom composition

The product shell, Home shelves, details hero, storage summary, deterministic simulated QR,
poster status marks, controller diagnostic diagram, operation-log presentation, persistent warnings
and modal content are composed from public `DrawList` and `Painter` operations. These
product-specific layouts do not exist as kit components. Controller highlights use the current
model input snapshot. The log sheet consumes the model's bounded 128-line fixture buffer and
eight-line offset directly, including loading, empty and failed states with retry/back actions.

Sheets and confirmations use custom composition because the simulation owns the focus stack and
publishes resolved rows. Instantiating the kit's stateful `Sheet` or `Dialog` would create a
second input and focus model. The custom overlays use the same theme tokens, a dim backdrop,
exclusive simulation focus, and the simulation's focused/default action.

The reference focus glow is approximated with the kit's SDF glow plus a four-unit orange ring.
The reference blur is approximated by a dim scrim and opaque warm panel. The renderer requests a
kit glass capture for overlay frames, but the custom panel does not currently sample that blurred
texture.

Right-anchored sheets use a nominal 220 ms cubic ease at the current fixed 60 Hz presentation timestep for their horizontal entrance and scrim fade, inside
the reference's 180–280 ms range. Kit components receive the simulation's reduced-motion flag;
custom sheets complete immediately when reduced motion is enabled. Other custom surfaces are
frame-stable compositions and do not invent additional motion. Recorded comparison captures draw
settling frames before capture, while reduced-motion and screenshot fixture states remain
deterministic.

## Typography and international text

Barlow Regular, Medium and SemiBold plus Barlow Condensed Medium and SemiBold are baked into Hui
SDF atlases from the frozen shared font files. The current baker's `european` range asks each
Barlow file for code points it does not contain and reports 151 skipped requests. Basic Latin
and the fixture's supported accented Latin render from Barlow. The renderer checks the preferred
face and selects the shared Noto CJK atlas when a missing code point is available there; coverage
checks confirm base Greek, Cyrillic and CJK glyph coverage. The frozen font files do not
contain Greek accented alpha/omicron U+03AC and U+03CC; these remain missing in the Greek
fixture. The original title bytes are preserved. Adding a face with those glyphs requires a
coordinated shared-asset revision rather than a unilateral fixture-font replacement.

Noto CJK and Noto Arabic fixture subsets are baked into two remaining Hui font slots. The view
selects these fallbacks for dynamic strings containing CJK or Arabic code points. Hui does not
shape scripts or perform bidirectional layout, so the Arabic fixture has glyph coverage but is
displayed unshaped and left-to-right. Fallback selection currently applies to the complete
dynamic string, rather than splitting a mixed-script string into shaped runs.

## Artwork lifecycle

All eight shared synthetic posters and the shared hero are uploaded once when the renderer is
created: a fixed nine-texture cache. Missing, pending or failed cover art uses an ID-derived stable
placeholder without changing cell geometry. The delayed-art fixture first exposes that placeholder
and a later deterministic simulation tick clears `art_pending`, at which point the renderer uses
the already uploaded poster. This validates state transition and layout stability; it does not
claim asynchronous file decoding, background upload, eviction or generation tracking.

Capture sidecars expose presentation-only diagnostics: the selected game ID resolved by the
library grid, the number of grid cell callbacks issued for that frame, the fixed artwork texture
count and the native framebuffer dimensions. These counters verify bounded rendering and focus
mapping without adding timers or resource polling.

## Host compatibility

The pinned kit assumes desktop OpenGL 4.5. The isolated dependency patch adapts it to the macOS
OpenGL 4.1 core profile by removing explicit uniform locations, replacing base-instance drawing,
and using the portable GL declaration wrapper. PNG decoding is supplied as a header-only pinned
dependency because the kit exposes texture upload but no image decoder. This host work is outside
the shared framework checkout and is required only for the macOS comparison host.
