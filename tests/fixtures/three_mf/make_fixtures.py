#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds the sliced-3MF test archives from src/.

single_plate.gcode.3mf  plate 9 of a larger project sliced alone, as a slicer
                        uploads it: deflated G-code, its .md5, stored renders
                        and slice_info.config
multi_plate.gcode.3mf   plates 2 and 5 sliced, with a slice_info block each
project.3mf             renders and settings, no sliced plate
commented.gcode.3mf     single_plate's entries behind an archive comment
bad_md5.gcode.3mf       single_plate with a .md5 naming other bytes
far_info.gcode.3mf      slice_info.config further from the end than one tail read
wide_cd.gcode.3mf       a central directory larger than one tail read

Run from anywhere: python3 tests/fixtures/three_mf/make_fixtures.py
"""

import hashlib
import io
import os
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "src")
STAMP = (2026, 10, 15, 16, 16, 0)


def read(name):
    with open(os.path.join(SRC, name), "rb") as f:
        return f.read()


def add(z, name, data, deflate):
    info = zipfile.ZipInfo(name, STAMP)
    info.compress_type = zipfile.ZIP_DEFLATED if deflate else zipfile.ZIP_STORED
    z.writestr(info, data)


def write(name, entries, comment=b""):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        for entry in entries:
            add(z, *entry)
        z.comment = comment
    with open(os.path.join(HERE, name), "wb") as f:
        f.write(buf.getvalue())


def plate_entries(n, gcode, thumb):
    md5 = hashlib.md5(gcode).hexdigest().upper().encode()
    return [
        (f"Metadata/plate_{n}.png", thumb, False),
        (f"Metadata/plate_{n}_small.png", thumb, False),
        (f"Metadata/plate_{n}.gcode.md5", md5, True),
        (f"Metadata/plate_{n}.gcode", gcode, True),
    ]


def main():
    gcode = read("plate.gcode")
    small = read("plate_small.png")
    small_b = read("plate_b_small.png")
    slice_info = read("slice_info.config")
    types = (b'<?xml version="1.0" encoding="UTF-8"?>\n<Types xmlns='
             b'"http://schemas.openxmlformats.org/package/2006/content-types"/>\n')

    single = ([("[Content_Types].xml", types, True),
               ("Metadata/plate_1.png", small_b, False)] +
              plate_entries(9, gcode, small) +
              [("Metadata/slice_info.config", slice_info, True)])
    write("single_plate.gcode.3mf", single)
    write("commented.gcode.3mf", single, comment=b"sliced for the tests " * 20)

    # Plate 5 prints a shorter job on two filaments.
    plate_b = slice_info.replace(b'value="9"', b'value="5"')
    plate_b = plate_b.replace(b'value="5238"', b'value="1800"')
    plate_b = plate_b.replace(b'value="23.73"', b'value="12.5"')
    plate_b = plate_b.replace(
        b"</layer_filament_lists>",
        b'</layer_filament_lists>\n    <filament id="2" type="PLA" color="#FF0000" '
        b'used_m="1.5" used_g="4.25"/>')
    plate_a = slice_info.replace(b'value="9"', b'value="2"')
    a_start = plate_a.index(b"<plate>")
    a_end = plate_a.index(b"</plate>") + len(b"</plate>")
    b_block = plate_b[plate_b.index(b"<plate>"):plate_b.index(b"</plate>") + len(b"</plate>")]
    multi_info = plate_a[:a_end] + b"\n  " + b_block + plate_a[a_end:]
    gcode_b = gcode.replace(b"total layer number: 200", b"total layer number: 90")
    multi = ([("[Content_Types].xml", types, True)] +
             plate_entries(2, gcode, small) + plate_entries(5, gcode_b, small_b) +
             [("Metadata/slice_info.config", multi_info, True)])
    write("multi_plate.gcode.3mf", multi)

    bad = [e if not e[0].endswith(".md5") else (e[0], b"0" * 32, True) for e in single]
    write("bad_md5.gcode.3mf", bad)

    # 80KB of incompressible bytes between slice_info and the central directory.
    filler = hashlib.sha256(b"filler").digest() * (80 * 1024 // 32)
    far = ([e for e in single if e[0] != "Metadata/slice_info.config"] +
           [("Metadata/slice_info.config", slice_info, True),
            ("Metadata/filler.bin", filler, False)])
    write("far_info.gcode.3mf", far)

    # Enough long-named empty entries that the central directory outgrows 64KB.
    pad = [(f"Metadata/padding/{i:04d}_" + "x" * 80, b"", False) for i in range(600)]
    write("wide_cd.gcode.3mf", pad + single)

    project = [("[Content_Types].xml", types, True),
               ("Metadata/plate_1.png", small, False),
               ("3D/3dmodel.model", b"<model/>", True)]
    write("project.3mf", project)


if __name__ == "__main__":
    main()
