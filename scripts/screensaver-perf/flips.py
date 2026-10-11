# SPDX-License-Identifier: GPL-3.0-or-later
"""Presented-frame statistics from an strace ioctl log.

usage: flips.py <strace log>

The dumb-DRM binary presents with DRM_IOCTL_MODE_PAGE_FLIP and the EGL binary with
DRM_IOCTL_MODE_ATOMIC, so both count as a presented frame. Prints one FLIPS line.
"""

import re
import statistics
import sys

FLIP_IOCTLS = ("DRM_IOCTL_MODE_PAGE_FLIP", "DRM_IOCTL_MODE_ATOMIC")
TIMESTAMP = re.compile(r"(\d{10}\.\d+) ioctl")


def flip_times(lines):
    """Timestamps, in seconds, of every presenting ioctl call. A resumed call is the same flip."""
    times = []
    for line in lines:
        if "resumed" in line or not any(name in line for name in FLIP_IOCTLS):
            continue
        match = TIMESTAMP.search(line)
        if match:
            times.append(float(match.group(1)))
    return times


def flips_line(times):
    if len(times) < 3:
        return f"FLIPS n={len(times)} insufficient"
    gaps = sorted((b - a) * 1000 for a, b in zip(times, times[1:]))
    count = len(gaps)

    def quantile(p):
        return gaps[min(count - 1, int(count * p))]

    span = times[-1] - times[0]
    return (f"FLIPS fps={(len(times) - 1) / span:.1f} p10={quantile(.1):.1f} p50={quantile(.5):.1f} "
            f"p90={quantile(.9):.1f} p99={quantile(.99):.1f} max={gaps[-1]:.1f} "
            f"stdev={statistics.pstdev(gaps):.1f}")


def main(argv):
    with open(argv[1]) as log:
        print(flips_line(flip_times(log)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
