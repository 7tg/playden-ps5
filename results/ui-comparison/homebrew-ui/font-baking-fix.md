# CFF fallback font corruption

The PS5 screenshots from 2026-10-04 at 19.35.42 (Greek Details title) and
19.36.31 (CJK library tile) expose malformed **baked atlas pixels**. This is
reproducible on the host without a GPU or console.

The pinned Hui baker calls `stbtt_GetCodepointSDF` for every face. Its bundled
stb implementation handles line and quadratic segments in both
`stbtt__compute_crossings_x` and `stbtt_GetGlyphSDF`, but omits
`STBTT_vcubic`. `NotoSansCJKsc-Regular.otf` has CFF cubic outlines. Its normal
bitmap rasterization does support cubic curves; its analytic SDF silently
loses those curves. Barlow has no Greek, so `font_for` selects this same Noto
fallback for Greek titles. Straight outline portions surviving while curved
letters become blocks matches the supplied screenshots.

Tracing the rest of the pipeline found no conversion that could restore the
missing outlines: `Font::load` copies atlas bytes, `Font::layout` derives UVs
from the serialized glyph rectangles, and `GlBatch::create_font_texture`
uploads those bytes as `GL_R8` / `GL_RED` with unpack alignment 1. The corrupted
silhouette is already present before these runtime stages. No renderer patch
is needed for this diagnosed defect.

`patches/homebrew-ui-cff-sdf.patch` changes only the offline baker in the
isolated pinned framework checkout. For CFF faces it uses stb's cubic-capable
curve flattening at 0.05 atlas pixels, then computes segment distances and
nonzero winding over closed contours. It keeps the existing eight-pixel SDF
range, edge value 128, atlas layout and baseline metrics. Empty glyphs retain
advance without a quad. TrueType faces continue through the original analytic
SDF path. `scripts/build-homebrew.sh` applies both recorded patches before
baking; normal CMake baker dependencies also rebuild host atlases.

## Verification

The regression loads the actual `.huifont` through `Font::load` and compares
every baked glyph against stb's ordinary cubic-capable bitmap rasterizer at
the same scale and baseline. It checks atlas bounds/offsets and empty-space
metrics, and excludes partially antialiased reference pixels (coverage
33–223) from mismatch counts because area coverage and pixel-center SDF
classification differ at edges. The limit is 10% of the union of foreground
pixels per glyph. The production change that breaks this test is returning
CFF faces to `stbtt_GetCodepointSDF`.

| Font | Glyphs checked | Original failures | Corrected failures | Corrected worst interior mismatch |
| --- | ---: | ---: | ---: | ---: |
| Noto CJK (includes Greek) | 364 | 257 | 0 | 0% |
| Barlow regular | 323 | 0 | 0 | 0% |
| Noto Arabic | 127 | 0 | 0 | 0% |

The original CFF atlas's worst interior mismatch was 97.29%. A preliminary
comparison including partially antialiased pixels found 271/364 failures
before and none after, with 4.53% worst edge-inclusive mismatch after. The
interior-only check avoids false failures on small TrueType punctuation.

All five Barlow atlases and the Arabic atlas retain identical SHA-256 hashes.
Only the CJK atlas changed, from
`bdcad855e131f150a066cd7ff250c921fa58ba5fcc171a5b75e0b4fe4dee0091` to
`a2158de5866504c2d8ac20b6273575cf879a8d22841771628049792ee53c3947`.
Both patches apply cleanly in order to a fresh archive of pinned revision
`8a716f294835e26a24ef5612f1535aabffef4080`; the resulting baker matches the
working isolated checkout. Existing-patch preparation also succeeds.

Host verification: `cmake --build build/homebrew-host` and
`ctest --test-dir build/homebrew-host --output-on-failure` passed all 10 tests,
including Latin, CFF, Arabic, and shared-fixture verification. Source font
fixtures and artwork were not modified. Native font output in
`build/homebrew-fonts` was regenerated through the preparation script.

Physical PS5 verification remains outstanding; this establishes the offline
root cause and corrected atlas contents, not a new console capture. Arabic
shaping and BiDi are explicitly deferred; the Arabic test covers atlas
integrity only.

## Accented Greek coverage follow-up

A subsequent host capture exposed a second, independent issue after the cubic
outlines were corrected: the fixture's Noto CJK face lacks U+03CC (ό), U+03AF
(ί), and U+03AC (ά). The title contains four occurrences of those codepoints,
so the runtime replaced four letters with question marks. Silhouette-only
tests did not detect missing glyphs because absent codepoints have no atlas
records to inspect.

Added the licensed, unmodified `assets/fonts/NotoSans-Regular.ttf` as a
supplemental face outside immutable fixtures. Its local source revision,
font/license hashes and packaging details are in `assets/fonts/README.md`.
Both bake paths produce `noto-greek-fallback.huifont` and include the OFL
license. The renderer must load this sixth face and select it for Greek text
before the CJK fallback.

The regression now accepts required text and calls `Font::has_glyph` for
every codepoint. CMake reads the complete title from fixture game 900003;
it does not duplicate or alter the title. With the corrected CJK atlas this
check reproduces four missing-glyph failures. With the supplemental Greek
atlas it reports no missing characters and all **463 glyph silhouettes pass**,
with 0% worst interior mismatch. The four font checks and immutable-fixture
verification pass. Host/native generated font directories include the
matching license. A fresh console capture remains pending; Arabic shaping
and BiDi remain outside this fix.
