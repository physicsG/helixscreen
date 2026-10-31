#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Power-loss recovery task T1: time helix_gcode_reader.scan_to_offset over a
file to near its end, the way a resume plan scans it.

Each run is a fresh child process, so peak RSS is that run's own, and the
median of the runs is reported. Prints wall time, CPU time (user + system),
peak RSS and the interpreter's RSS before the scan.

    plr_t1_scan_timing.py FILE [--runs 3] [--tail-bytes 1024] [--reader-dir DIR]

--reader-dir is the directory holding helix_gcode_reader.py (default: this
script's parent's parent, i.e. moonraker-plugin/). Python 3.8, stdlib only.
"""

import argparse
import json
import os
import subprocess
import sys
import time


def _rss_kib(field):
    with open("/proc/self/status") as f:
        for line in f:
            if line.startswith(field + ":"):
                return int(line.split()[1])
    return -1


def child(path, end, reader_dir):
    sys.path.insert(0, reader_dir)
    import helix_gcode_reader

    rss_before = _rss_kib("VmRSS")
    t0 = time.monotonic()
    c0 = os.times()
    with open(path, "rb") as f:
        result = helix_gcode_reader.scan_to_offset(f, end)
    c1 = os.times()
    t1 = time.monotonic()
    print(json.dumps({
        "wall_s": t1 - t0,
        "cpu_s": (c1.user - c0.user) + (c1.system - c0.system),
        "peak_rss_kib": _rss_kib("VmHWM"),
        "rss_before_kib": rss_before,
        "offset": result.offset,
        "layer": result.state_at_head.layer,
        "window_lines": len(result.window),
    }))


def median(values):
    s = sorted(values)
    n = len(s)
    return s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) / 2.0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("file")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--tail-bytes", type=int, default=1024,
                    help="scan to this many bytes before the end of the file")
    ap.add_argument("--reader-dir",
                    default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument("--child", action="store_true", help=argparse.SUPPRESS)
    args = ap.parse_args(argv)

    size = os.path.getsize(args.file)
    end = max(0, size - args.tail_bytes)
    if args.child:
        child(args.file, end, args.reader_dir)
        return 0

    print("python %s, %s, file %d bytes, scan to %d" % (
        sys.version.split()[0], args.file, size, end))
    runs = []
    for i in range(args.runs):
        out = subprocess.run(
            [sys.executable, os.path.abspath(__file__), args.file, "--child",
             "--tail-bytes", str(args.tail_bytes), "--reader-dir", args.reader_dir],
            check=True, stdout=subprocess.PIPE, universal_newlines=True,
        ).stdout
        run = json.loads(out.strip().splitlines()[-1])
        runs.append(run)
        print("run %d: wall %.2f s, cpu %.2f s, peak RSS %.1f MiB (%.1f MiB before scan), "
              "reached %d, layer %s" % (
                  i + 1, run["wall_s"], run["cpu_s"], run["peak_rss_kib"] / 1024.0,
                  run["rss_before_kib"] / 1024.0, run["offset"], run["layer"]))
    print("median: wall %.2f s, cpu %.2f s, peak RSS %.1f MiB" % (
        median([r["wall_s"] for r in runs]), median([r["cpu_s"] for r in runs]),
        median([r["peak_rss_kib"] for r in runs]) / 1024.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
