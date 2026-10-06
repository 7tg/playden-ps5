# Generic game title icon

`generic-title-icon.png` is the PS5 Home icon for a prepared game whose Steam cover cannot be fetched. It is a
512 × 512 RGB PNG and is packaged as `assets/generic-title-icon.png`. Its rendered source is
`generic-title-icon-source.png`, a 1024 × 1024 PNG returned by the Images API using `gpt-image-2.5-sunburst` at high
quality, with `background: opaque`.

The floppy disk keeps a thin margin, so its corner markers stay clear of the rounded Home tile.

Regenerate the packaged tile from the repository root with ImageMagick:

```sh
magick assets/branding/generic-title-icon-source.png -filter Lanczos \
  -resize 512x512 -alpha off -strip PNG24:assets/branding/generic-title-icon.png
```

## Prompt

A 3.5-inch floppy disk seen straight on, perfectly square to the viewer and not tilted. Body in warm orange satin
plastic, metal shutter in dark brushed charcoal at the top, the label area blank and slightly lighter orange, the small
square notch markers in the bottom corners and the top-left corner clearly visible. Smooth modern 3D product render,
not pixel art, not low-res, not photographic clutter. The disk has softly rounded corners and a subtle drop shadow onto
the background.

Square video game console home-screen tile: the whole subject is visible, centered and as large as possible, filling
about 92% of the square, on a flat near-black charcoal background (#0B0B0B). Only a very thin even margin, about 4% of
the width, on every side, so every corner and edge of the subject is fully inside the canvas and nothing is cropped.
Polished soft 3D render, smooth satin material, gentle studio lighting from the top left, subtle inner shadows and a
soft highlight on edges. Palette: warm Playden orange (#E8782A to #F08A3C) and near-black charcoal (#0B0B0B to
#1A1A1A). No text, no letters, no numbers, no logos, no watermark.
