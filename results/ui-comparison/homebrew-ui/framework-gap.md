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
poster status marks, persistent warnings and modal content are composed from public `DrawList`
and `Painter` operations. These product-specific layouts do not exist as kit components.

Sheets and confirmations use custom composition because the simulation owns the focus stack and
publishes resolved rows. Instantiating the kit's stateful `Sheet` or `Dialog` would create a
second input and focus model. The custom overlays use the same theme tokens, a dim backdrop,
exclusive simulation focus, and the simulation's focused/default action.

The reference focus glow is approximated with the kit's SDF glow plus a four-unit orange ring.
The reference blur is approximated by a dim scrim and opaque warm panel. The renderer requests a
kit glass capture for overlay frames, but the custom panel does not currently sample that blurred
texture.

## Typography and international text

Barlow Regular, Medium and SemiBold plus Barlow Condensed Medium and SemiBold are baked into Hui
SDF atlases from the frozen shared font files. The current baker's `european` range asks each
Barlow file for code points it does not contain and reports 151 skipped requests. Basic Latin
and the fixture's supported accented Latin render from Barlow. The renderer checks the preferred
face and selects the shared Noto CJK atlas when a missing code point is available there; coverage
checks confirm that this supplies the fixture's Greek and Cyrillic strings as well as CJK.

Noto CJK and Noto Arabic fixture subsets are baked into two remaining Hui font slots. The view
selects these fallbacks for dynamic strings containing CJK or Arabic code points. Hui does not
shape scripts or perform bidirectional layout, so the Arabic fixture has glyph coverage but is
displayed unshaped and left-to-right. Fallback selection currently applies to the complete
dynamic string, rather than splitting a mixed-script string into shaped runs.

## Artwork lifecycle

All eight shared synthetic posters and the shared hero are uploaded once when the renderer is
created. Missing or failed cover art uses an ID-derived stable placeholder without changing cell
geometry. The renderer can display the simulation's failed-art state. `Game` currently exposes a
failure boolean but no delayed/loading generation or texture-ready transition, so the delayed-art
scenario cannot demonstrate asynchronous arrival inside this renderer yet.

## Host compatibility

The pinned kit assumes desktop OpenGL 4.5. The isolated dependency patch adapts it to the macOS
OpenGL 4.1 core profile by removing explicit uniform locations, replacing base-instance drawing,
and using the portable GL declaration wrapper. PNG decoding is supplied as a header-only pinned
dependency because the kit exposes texture upload but no image decoder. This host work is outside
the shared framework checkout and is required only for the macOS comparison host.
