#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Downscale + palette-quantize a curated subset of assets/images/printers/*.png
for the ESP32 packed asset container.

The storage partition cannot hold every printer's picture (80 renditions came
to 1.2MB against ~0.7MB free), so the firmware ships ESP32_PRINTERS: the
machines an add-on panel like the K-Touch drives, which are the DIY and
Klipper-converted printers without a stock screen, ranked by hardware_profile
telemetry, plus the FlashForge Adventurer 5M and 5M Pro, which a K-Touch drives
over the network. A printer not in the set shows generic-corexy, the widget's fallback.
Renditions fit a 200x200 box: the home widget's image cell is about 230x210
on the 800x480 panel, and the firmware decodes the PNG and scales it at draw
time, so a larger rendition costs flash and decode time and shows nothing more.
A fixed width would not do: the source art is cropped to its content, so a tall
printer at a fixed width comes out taller, and bigger, than the cell can show.

Quality is favored over squeeze (headroom is ample post-container): each
image is palette-quantized to up to 256 colors via Pillow's FASTOCTREE
method, the only quantize method that quantizes the alpha channel jointly
with color instead of reducing to binary transparency. Falls back to fewer
colors only if 256 doesn't help, and to a plain resized RGBA PNG (no
quantization) if quantizing would produce a LARGER file than the resize
alone (rare, but happens on some near-flat/low-color source art).

Usage:
    python3 -m venv /tmp/esp32imgs-venv && . /tmp/esp32imgs-venv/bin/activate
    pip install Pillow
    python3 scripts/esp32_printer_images.py [--out DIR]

If an image in the set is missing or fails to process, or Pillow is not
installed, this script exits non-zero rather than shipping a partial set.
esp32_stage_assets.py calls generate() itself when the renditions are absent
or stale, so a build never packs without them.
"""

import argparse
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SOURCE_DIR = REPO_ROOT / "assets" / "images" / "printers"
DEFAULT_OUT = REPO_ROOT / "build" / "esp32_printer_images"

TARGET_BOX = 200

# generic-corexy first: it is the fallback every other printer resolves to.
ESP32_PRINTERS = (
    "generic-corexy",
    "voron-v2", "voron-trident", "voron-v0", "voron-switchwire", "voron-legacy",
    "sovol-sv08", "sovol-sv08-max", "sovol-zero",
    "ratrig-vcore3", "ratrig-vcore4", "ratrig-vminion",
    "zerog-hydra-255", "zerog-hydra-370", "zerog-nebula", "zerog-nebula-370",
    "vzbot", "doron_velta", "pfa-micron", "pfa-stealthfork",
    "creality-ender-3", "creality-ender-5", "creality-cr10", "prusa-mk4",
    "flashforge-adventurer-5m", "flashforge-adventurer-5m-pro",
)


def format_bytes(n: int) -> str:
    return f"{n:,} B ({n / 1024:.1f} KiB)"


def render_one(src: Path, dest: Path) -> tuple[int, int]:
    """Downscale + quantize a single PNG. Returns (orig_bytes, out_bytes)."""
    from PIL import Image

    orig_bytes = src.stat().st_size

    im = Image.open(src).convert("RGBA")
    scale = TARGET_BOX / max(im.width, im.height)
    target = (round(im.width * scale), round(im.height * scale))
    resized = im.resize(target, Image.LANCZOS) if target != im.size else im

    import io

    def encode(image: Image.Image) -> bytes:
        buf = io.BytesIO()
        image.save(buf, "PNG", optimize=True)
        return buf.getvalue()

    resized_bytes = encode(resized)
    best = resized_bytes

    # Try 256 colors first ("favoring quality"); only drop to fewer colors
    # if 256 didn't already beat the plain resized RGBA PNG.
    for colors in (256, 128, 64):
        quantized = resized.quantize(colors=colors, method=Image.Quantize.FASTOCTREE,
                                     dither=Image.Dither.FLOYDSTEINBERG)
        encoded = encode(quantized)
        if len(encoded) < len(best):
            best = encoded
        if len(best) < len(resized_bytes):
            break

    dest.parent.mkdir(parents=True, exist_ok=True)
    with open(dest, "wb") as f:
        f.write(best)

    return orig_bytes, len(best)


def is_fresh(out_dir: Path) -> bool:
    """True when out_dir holds exactly the ESP32_PRINTERS renditions, each newer
    than its source picture and than this script."""
    expected = {f"{name}.png" for name in ESP32_PRINTERS}
    if not out_dir.is_dir() or {f.name for f in out_dir.iterdir()} != expected:
        return False
    oldest_output = min((out_dir / name).stat().st_mtime for name in expected)
    newest_input = max([(SOURCE_DIR / name).stat().st_mtime for name in expected
                        if (SOURCE_DIR / name).exists()] + [Path(__file__).stat().st_mtime])
    return oldest_output >= newest_input


def generate(out_dir: Path = DEFAULT_OUT) -> int:
    """Render ESP32_PRINTERS into out_dir. Returns 0, or 1 after printing why."""
    try:
        import PIL  # noqa: F401
    except ImportError:
        print("FAIL: Pillow not installed, so the ESP32 printer pictures cannot be "
              "generated. Use a venv:\n"
              "  python3 -m venv /tmp/esp32imgs-venv && "
              "/tmp/esp32imgs-venv/bin/pip install Pillow",
              file=sys.stderr)
        return 1

    out_dir.mkdir(parents=True, exist_ok=True)

    sources = [SOURCE_DIR / f"{name}.png" for name in ESP32_PRINTERS]
    missing = [p.name for p in sources if not p.exists()]
    if missing:
        print(f"FAIL: ESP32_PRINTERS names images not in {SOURCE_DIR}: {', '.join(missing)}",
              file=sys.stderr)
        return 1

    rows = []
    total_orig = 0
    total_out = 0
    failures = []

    for src in sources:
        dest = out_dir / src.name
        try:
            orig_bytes, out_bytes = render_one(src, dest)
        except Exception as exc:  # noqa: BLE001 - report and keep going, but fail the run
            failures.append((src.name, str(exc)))
            continue
        rows.append((src.name, orig_bytes, out_bytes))
        total_orig += orig_bytes
        total_out += out_bytes

    print(f"ESP32 printer image pipeline: {SOURCE_DIR} -> {out_dir}")
    print(f"  Target box: {TARGET_BOX}x{TARGET_BOX}px, {len(ESP32_PRINTERS)} printers")
    print()
    print(f"  {'file':<38} {'orig':>14} {'packed':>14} {'ratio':>8}")
    for name, orig_bytes, out_bytes in rows:
        ratio = f"{100.0 * out_bytes / orig_bytes:.0f}%" if orig_bytes else "n/a"
        print(f"  {name:<38} {orig_bytes:>10,} B {out_bytes:>10,} B {ratio:>8}")
    print()
    print(f"  TOTAL: {len(rows)} images, {format_bytes(total_orig)} -> {format_bytes(total_out)}"
          f" ({100.0 * total_out / total_orig:.1f}%)" if total_orig else "  TOTAL: 0 images")

    if failures:
        print(file=sys.stderr)
        print(f"FAIL: {len(failures)} image(s) failed to process (all images must ship):",
              file=sys.stderr)
        for name, error in failures:
            print(f"  {name}: {error}", file=sys.stderr)
        return 1

    if len(rows) != len(sources):
        print(f"FAIL: only {len(rows)}/{len(sources)} images processed", file=sys.stderr)
        return 1

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT,
                        help=f"output directory (default: {DEFAULT_OUT})")
    args = parser.parse_args()
    return generate(args.out)


if __name__ == "__main__":
    sys.exit(main())
