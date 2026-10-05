#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["python-dotenv>=1.0,<2"]
# ///
"""Generate a native 4K background. Run with uv run scripts/generate-ps5-background.py.

Uses the Images generations endpoint, not image editing or local upscaling.
Reads OPENAI_API_KEY or OPENAI_KEY from the repository .env by default.
"""

import argparse
import base64
import json
import os
from pathlib import Path
import struct
import sys
import urllib.error
import urllib.request

from dotenv import dotenv_values

ROOT = Path(__file__).resolve().parents[1]
SIZE = "3840x2160"
ENDPOINT = "https://api.openai.com/v1/images/generations"


def png_dimensions(data):
    if len(data) < 33 or data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise ValueError("API response did not contain a PNG image")
    return struct.unpack(">II", data[16:24])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--env", type=Path, default=ROOT / ".env")
    parser.add_argument("--prompt-file", type=Path,
                        default=ROOT / "assets/branding/ps5-background-prompt.txt")
    parser.add_argument("--out", type=Path,
                        default=ROOT / "output/imagegen/playden-background-native-4k.png")
    parser.add_argument("--model", default="gpt-image-2.5-sunburst")
    parser.add_argument("--quality", choices=["low", "medium", "high", "xhigh", "max"],
                        default="high")
    parser.add_argument("--dry-run", action="store_true",
                        help="Show non-secret request settings without contacting the API")
    args = parser.parse_args()
    prompt = args.prompt_file.read_text(encoding="utf-8").strip()
    if not prompt:
        parser.error("Prompt file is empty")
    payload = dict(model=args.model, prompt=prompt, size=SIZE, quality=args.quality,
                   n=1, output_format="png", background="opaque")
    if args.out.exists():
        parser.error("Output already exists; choose a different --out to preserve it")
    if args.dry_run:
        print(json.dumps({"endpoint": ENDPOINT, "output": str(args.out), **payload}, indent=2))
        return

    values = dotenv_values(args.env, interpolate=False) if args.env.is_file() else {}
    key = (values.get("OPENAI_API_KEY") or values.get("OPENAI_KEY")
           or os.environ.get("OPENAI_API_KEY") or os.environ.get("OPENAI_KEY"))
    if not key:
        parser.error("Set OPENAI_API_KEY or OPENAI_KEY in --env or the environment")
    request = urllib.request.Request(
        ENDPOINT, data=json.dumps(payload).encode("utf-8"), method="POST",
        headers={"Authorization": "Bearer " + key, "Content-Type": "application/json"})
    print(f"Requesting one {SIZE} PNG with {args.model}, quality={args.quality}...", flush=True)
    # No automatic retries: an interrupted paid generation may have completed.
    try:
        with urllib.request.urlopen(request, timeout=1200) as response:
            result = json.load(response)
            request_id = response.headers.get("x-request-id")
    except urllib.error.HTTPError as exc:
        # Do not print response bodies, which can echo credential fragments.
        raise RuntimeError(f"Image API returned HTTP {exc.code}; no image was saved") from None
    except urllib.error.URLError:
        raise RuntimeError("Image API connection failed; no automatic retry was attempted") from None

    images = result.get("data") or []
    if len(images) != 1 or not images[0].get("b64_json"):
        raise ValueError("Expected exactly one base64-encoded image from the API")
    data = base64.b64decode(images[0]["b64_json"], validate=True)
    dimensions = png_dimensions(data)
    if dimensions != (3840, 2160):
        raise ValueError(f"API returned {dimensions}; expected (3840, 2160). No resizing or saving performed")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("xb") as output:
        output.write(data)
    metadata = {**payload, "request_id": request_id, "usage": result.get("usage"),
                "returned_dimensions": list(dimensions), "locally_resized": False}
    args.out.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Saved verified native {SIZE} PNG: {args.out}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, OSError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        sys.exit(1)
