# PS5 home-screen artwork

`icon0.png` is the packaged 512 × 512 RGB PNG tile. Its rendered source is
`icon0-source.png`, generated with the built-in image tool using the original
flat PS5 tile as a geometry reference and the supplied macOS app screenshot
as a material and lighting reference. The orange mark now has raised edges,
soft highlights, an amber gradient, and a beveled play cutout. The charcoal
background fills the square. The generation prompt is in `icon0-prompt.md`.

`icon0-source.svg` retains the original flat geometry, adapted from the
macOS Playden icon at revision
`de952b3d49167e635945ada88cd8533e07d40d44`
(`docs/design/icons/playden-flat.svg`).

Regenerate from the repository root with ImageMagick:

```sh
magick sce_sys/icon0-source.png -filter Lanczos \
  -resize 512x512 -alpha off -strip PNG24:sce_sys/icon0.png
```

Both PS5 packaging paths use this icon and restage when it changes.

`background-source.png` is the native 3840 × 2160 PNG returned by the Images
API using `gpt-image-2.5-sunburst`, generated from scratch with no local
resizing. Non-secret request metadata is retained in `background-generation.json`.
Generated previews and alternate outputs under `output/imagegen/` stay local.
Generate another
version with:

```sh
uv run scripts/generate-ps5-background.py \
  --out output/imagegen/playden-background-native-4k-v2.png
```

The script reads `OPENAI_API_KEY` or `OPENAI_KEY` from the repository `.env`,
requests one high-quality PNG at exactly 3840 × 2160, and verifies dimensions
before saving. Existing output files are preserved. Use `--dry-run` to inspect
the request without an API call; `--prompt-file`, `--model`, and `--quality`
allow explicit customization. The prompt lives in
`assets/branding/ps5-background-prompt.txt`.

Selection (`pic0.dds`) and launch (`pic1.dds`) backgrounds both use the approved
native 4K image. Both PS5 packaging paths copy these repository assets and
restage when either changes. Regenerate the DDS pair without resizing:

```sh
uv run scripts/prepare-ps5-background.py
```

This uses ISPCTextureCompressor's slow BC7 profile to produce 3840 × 2160,
single-image BC7_UNORM DX10 DDS files without mipmaps, matching the native-app
boilerplate's `tools/validate-assets.sh` requirements. The PNG is retained as
the source; it is not included in `sce_sys/` in the packaged title.
