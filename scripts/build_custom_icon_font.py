#!/usr/bin/env python3
# Copyright (C) 2025-2026 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Build assets/fonts/helix-custom-icons.ttf from the SVGs in assets/fonts/custom_icons/.

Icons that MDI does not have (brand marks such as the Spoolman logo) live here as
single-path 24x24 SVGs drawn on the MDI grid. This script compiles them into a TTF
with MDI's exact metrics, and scripts/regen_mdi_fonts.sh merges that TTF into every
mdi_icons_*.c face, so a custom glyph is an ordinary `<icon src="...">` that takes
the text colour like any other.

Adding a glyph:
  1. Draw it as one <path> (holes as counter-wound or overlapping subpaths; the
     outline is simplified here) in a 24x24 viewBox, y down, same padding as MDI.
  2. Add it to GLYPHS below with the next free codepoint in the U+FE000 block.
  3. Add the codepoint to CUSTOM_ICONS in scripts/regen_mdi_fonts.sh and the name to
     include/ui_icon_codepoints.h, then run this script and `make regen-fonts`.

Needs: pip install fonttools skia-pathops   (only to run this script; the TTF is
checked in, so building the app does not need either)
"""

import sys
import xml.etree.ElementTree as ET
from pathlib import Path

try:
    import pathops
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.cu2quPen import Cu2QuPen
    from fontTools.pens.transformPen import TransformPen
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.svgLib.path import parse_path
except ImportError as e:
    sys.exit(f"ERROR: {e}. Install with: pip install fonttools skia-pathops")

ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "assets" / "fonts" / "custom_icons"
OUT = ROOT / "assets" / "fonts" / "helix-custom-icons.ttf"

# codepoint -> SVG basename. The U+FE000 block sits far above MDI's highest
# codepoint (U+F1D17), so MDI growth never lands on one of these.
GLYPHS = {
    0xFE000: "spoolman",
    0xFE001: "spool",
    0xFE002: "spool_outline",
    0xFE003: "spool_alert",
}

# MDI's metrics: 512 units per em, a 24-unit design grid whose top edge sits at
# the ascender. Matching them makes a custom glyph line up with MDI glyphs in
# the merged face at every size.
UPM = 512
ASCENT = 448
DESCENT = -64
GRID = UPM / 24


def svg_path_data(svg_file: Path) -> str:
    root = ET.parse(svg_file).getroot()
    paths = root.findall(".//{http://www.w3.org/2000/svg}path")
    if len(paths) != 1:
        sys.exit(f"ERROR: {svg_file} must hold exactly one <path>, found {len(paths)}")
    return paths[0].get("d")


def build_glyph(d: str):
    # 24-grid, y down  ->  font units, y up
    outline = pathops.Path()
    parse_path(d, TransformPen(outline.getPen(), (GRID, 0, 0, -GRID, 0, ASCENT)))
    # Simplify resolves overlaps and gives outer/inner contours the opposite
    # windings TrueType's nonzero fill needs, whatever direction the SVG used.
    outline.simplify(fix_winding=True)
    pen = TTGlyphPen(None)
    outline.draw(Cu2QuPen(pen, max_err=0.5, reverse_direction=True))
    return pen.glyph()


def main() -> int:
    order = [".notdef"] + [f"uni{cp:05X}" for cp in GLYPHS]
    glyphs = {".notdef": TTGlyphPen(None).glyph()}
    metrics = {".notdef": (UPM, 0)}
    cmap = {}
    for cp, name in GLYPHS.items():
        gname = f"uni{cp:05X}"
        glyph = build_glyph(svg_path_data(SRC_DIR / f"{name}.svg"))
        glyph.recalcBounds(None)
        glyphs[gname] = glyph
        # hmtx's left side bearing must equal xMin or renderers shift the glyph.
        metrics[gname] = (UPM, glyph.xMin)
        cmap[cp] = gname

    fb = FontBuilder(UPM, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=ASCENT, descent=DESCENT)
    fb.setupNameTable({"familyName": "HelixScreen Custom Icons", "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=ASCENT, sTypoDescender=DESCENT, usWinAscent=ASCENT,
                usWinDescent=-DESCENT)
    fb.setupPost()
    # Fixed timestamps keep the checked-in TTF byte-identical across rebuilds.
    fb.updateHead(created=0, modified=0)
    fb.font.recalcTimestamp = False
    fb.save(str(OUT))
    print(f"Wrote {OUT.relative_to(ROOT)} ({len(GLYPHS)} glyphs)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
