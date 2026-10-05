#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["ispc_texcomp==1.0.1", "pillow>=11,<13"]
# ///
"""Encode the approved 4K PNG as PS5 BC7_UNORM backgrounds without resizing."""

import argparse
from pathlib import Path
import struct

import ispc_texcomp
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "sce_sys/background-source.png")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "sce_sys")
    args = parser.parse_args()
    with Image.open(args.source) as source:
        if source.size != (3840, 2160):
            parser.error("Source must already be exactly 3840 × 2160; no resizing is performed")
        image = source.convert("RGBA")
    width, height = image.size
    surface = ispc_texcomp.RGBASurface(image.tobytes(), width, height, width * 4)
    print("Compressing approved background with the slow BC7 quality profile...", flush=True)
    blocks = ispc_texcomp.compress_blocks_bc7(
        surface, ispc_texcomp.BC7EncSettings.from_profile("slow"))
    expected = (width // 4) * (height // 4) * 16
    if len(blocks) != expected:
        raise ValueError("BC7 encoder returned an unexpected payload size")
    # DDS_HEADER: one 2D texture, linear block payload, DX10 pixel format.
    header = struct.pack("<31I",
                         124, 0x81007, height, width, expected, 0, 1,
                         *([0] * 11),
                         32, 4, int.from_bytes(b"DX10", "little"), 0, 0, 0, 0, 0,
                         0x1000, 0, 0, 0, 0)
    # DDS_HEADER_DXT10: BC7_UNORM (98), TEXTURE2D (3), one array entry,
    # opaque alpha (3). No mipmaps or sRGB format flag.
    dds = b"DDS " + header + struct.pack("<5I", 98, 3, 0, 1, 3) + blocks
    args.out_dir.mkdir(parents=True, exist_ok=True)
    for name in ("pic0.dds", "pic1.dds"):
        destination = args.out_dir / name
        destination.write_bytes(dds)
        print(f"Wrote {destination} ({len(dds)} bytes)")


if __name__ == "__main__":
    main()
