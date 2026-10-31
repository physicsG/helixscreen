#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Build a large, realistic G-code file for the power-loss recovery scan timing
(task T1) by stacking copies of a real slicer file's print body.

The output keeps the source's start block and config block once, and repeats
everything from its first `;LAYER_CHANGE` to `; EXECUTABLE_BLOCK_END` until the
file reaches the target size. Each copy is raised by the source's top Z, and
SET_PRINT_STATS_INFO layer numbers continue across copies, so the line mix is
the slicer's own and layer numbering and Z still rise monotonically.

    make_t1_gcode.py SOURCE.gcode OUT.gcode [--size-mb 100]

Python 3.8, stdlib only.
"""

import argparse
import re
import sys

_Z_WORD = re.compile(rb"(?<=\sZ)(-?\d*\.?\d+)")
_CURRENT_LAYER = re.compile(rb"CURRENT_LAYER=(\d+)")
_TOTAL_LAYER = re.compile(rb"TOTAL_LAYER=(\d+)")


def split_source(data):
    body_start = data.index(b"\n;LAYER_CHANGE") + 1
    end_marker = data.find(b"; EXECUTABLE_BLOCK_END")
    body_end = end_marker if end_marker >= 0 else len(data)
    return data[:body_start], data[body_start:body_end], data[body_end:]


def top_z(body):
    return max(float(m.group(1)) for m in _Z_WORD.finditer(body))


def layer_count(body):
    layers = [int(m.group(1)) for m in _CURRENT_LAYER.finditer(body)]
    return max(layers) if layers else body.count(b"\n;LAYER_CHANGE") + 1


def shifted(body, copy, z_step, layers):
    if copy == 0:
        return body
    dz = copy * z_step
    dl = copy * layers

    def z(m):
        return b"%.3f" % (float(m.group(1)) + dz)

    def layer(m):
        return b"CURRENT_LAYER=%d" % (int(m.group(1)) + dl)

    out = []
    for line in body.splitlines(keepends=True):
        if line.startswith((b"G0 ", b"G1 ", b"G2 ", b"G3 ")) and b"Z" in line:
            line = _Z_WORD.sub(z, line)
        elif line.startswith(b"SET_PRINT_STATS_INFO"):
            line = _CURRENT_LAYER.sub(layer, line)
        out.append(line)
    return b"".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("source")
    ap.add_argument("out")
    ap.add_argument("--size-mb", type=float, default=100.0)
    args = ap.parse_args(argv)

    with open(args.source, "rb") as f:
        data = f.read()
    head, body, tail = split_source(data)
    z_step = top_z(body)
    layers = layer_count(body)
    target = int(args.size_mb * 1024 * 1024)
    copies = max(1, int(round((target - len(head) - len(tail)) / float(len(body)))))
    head = _TOTAL_LAYER.sub(b"TOTAL_LAYER=%d" % (layers * copies), head)

    with open(args.out, "wb") as out:
        out.write(head)
        for copy in range(copies):
            out.write(shifted(body, copy, z_step, layers))
        out.write(tail)
        size = out.tell()
    print("%s: %d copies of %d layers, %d bytes" % (args.out, copies, layers, size))
    return 0


if __name__ == "__main__":
    sys.exit(main())
