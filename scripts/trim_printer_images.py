#!/usr/bin/env python3
# Copyright (C) 2025-2026 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later
"""Crop assets/images/printers/*.png to their visible content.

Each image is cut to the bounding box of pixels with alpha > ALPHA_FLOOR plus
MARGIN px, with no aspect normalization: every transparent column is width the
home widget's contain-fit spends on nothing, and narrows the band its callout
chips need. regions.json is rewritten for every cropped image (new size, every
normalized point remapped to the same content pixel), so tags survive a crop.

Idempotent. Needs Pillow.

    scripts/trim_printer_images.py             # crop in place
    scripts/trim_printer_images.py --dry-run   # report what would change
    scripts/trim_printer_images.py --check     # exit 1 if any image needs a crop
"""

import argparse
import json
import sys
from pathlib import Path

from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent
IMAGES_DIR = REPO_ROOT / "assets" / "images" / "printers"
REGIONS = IMAGES_DIR / "regions.json"

ALPHA_FLOOR = 8
MARGIN = 2


def crop_box(im: Image.Image) -> tuple[int, int, int, int]:
    """Content bbox plus MARGIN, clamped to the image. The full image when there
    is no alpha channel or nothing visible."""
    if "A" not in im.getbands() and "transparency" not in im.info:
        return (0, 0, im.width, im.height)
    alpha = im.convert("RGBA").getchannel("A").point(lambda a: 255 if a > ALPHA_FLOOR else 0)
    bbox = alpha.getbbox()
    if bbox is None:
        return (0, 0, im.width, im.height)
    x0, y0, x1, y1 = bbox
    return (max(0, x0 - MARGIN), max(0, y0 - MARGIN),
            min(im.width, x1 + MARGIN), min(im.height, y1 + MARGIN))


def remap_point(pt, old_size, box):
    """A normalized point over the old image, renormalized over the crop box."""
    x0, y0, x1, y1 = box
    return [round((pt[0] * old_size[0] - x0) / (x1 - x0), 3),
            round((pt[1] * old_size[1] - y0) / (y1 - y0), 3)]


def remap_entry(entry: dict, box) -> dict:
    old_size = entry["size"]
    out = {}
    for key, val in entry.items():
        if key == "size":
            out[key] = [box[2] - box[0], box[3] - box[1]]
        elif val and isinstance(val[0], list):
            out[key] = [remap_point(p, old_size, box) for p in val]
        else:
            out[key] = remap_point(val, old_size, box)
    return out


def write_regions(regions: dict) -> None:
    # One entry per line, matching the tagger tool's output.
    lines = [f"  {json.dumps(k)}: {json.dumps(v)}" for k, v in regions.items()]
    REGIONS.write_text("{\n" + ",\n".join(lines) + "\n}\n")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--dry-run", action="store_true", help="report, change nothing")
    mode.add_argument("--check", action="store_true", help="exit 1 if any image needs a crop")
    args = ap.parse_args()

    regions = json.loads(REGIONS.read_text())
    untrimmed = []
    for path in sorted(IMAGES_DIR.glob("*.png")):
        im = Image.open(path)
        im.load()
        box = crop_box(im)
        if box == (0, 0, im.width, im.height):
            continue
        new_w, new_h = box[2] - box[0], box[3] - box[1]
        untrimmed.append(path.name)
        print(f"{path.name}: {im.width}x{im.height} -> {new_w}x{new_h}")
        if args.dry_run or args.check:
            continue
        info = {k: im.info[k] for k in ("transparency", "icc_profile", "dpi") if k in im.info}
        im.crop(box).save(path, "PNG", optimize=True, **info)
        if path.stem in regions:
            regions[path.stem] = remap_entry(regions[path.stem], box)

    if args.check:
        if untrimmed:
            print(f"FAIL: {len(untrimmed)} printer image(s) carry transparent margin; "
                  "run scripts/trim_printer_images.py", file=sys.stderr)
            return 1
        return 0
    if untrimmed and not args.dry_run:
        write_regions(regions)
    print(f"{len(untrimmed)} image(s) {'to crop' if args.dry_run else 'cropped'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
