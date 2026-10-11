# SPDX-License-Identifier: GPL-3.0-or-later
"""Latency percentiles from a cyclictest -h histogram (one thread).

usage: ctparse.py <histogram file> <arm> <label> <GATE_RAW line>

wakeup_probe writes the same histogram format, so its output parses here too. Prints one
RESULT line in the format summarize.py reads.
"""

import re
import sys

BIN = re.compile(r"^(\d+)\s+(\d+)\s*$")
TOTAL = re.compile(r"^#\s*Total:\s*(\d+)")
OVERFLOWS = re.compile(r"^#\s*Histogram Overflows:\s*(\d+)")
MAX_LATENCY = re.compile(r"^#\s*Max Latencies:\s*(\d+)")
HISTOGRAM_US = 20000


def parse_histogram(lines):
    counts = {}
    overflow = 0
    total_reported = None
    max_us = None
    for line in lines:
        match = BIN.match(line)
        if match:
            us = int(match.group(1))
            counts[us] = counts.get(us, 0) + int(match.group(2))
            continue
        match = TOTAL.match(line)
        if match:
            total_reported = int(match.group(1))
            continue
        match = OVERFLOWS.match(line)
        if match:
            overflow = int(match.group(1))
            continue
        match = MAX_LATENCY.match(line)
        if match:
            max_us = int(match.group(1))
    return counts, overflow, total_reported, max_us


def percentile(counts, overflow, fraction):
    """Smallest bin holding `fraction` of the samples; HISTOGRAM_US when it falls in the overflow."""
    total = sum(counts.values()) + overflow
    if total == 0:
        return -1
    need = total * fraction
    running = 0
    for us in sorted(counts):
        running += counts[us]
        if running >= need:
            return us
    return HISTOGRAM_US


def result_line(lines, arm, label, raw):
    fields = dict(re.findall(r"(\w+)=(\S+)", raw))
    counts, overflow, total_reported, max_us = parse_histogram(lines)
    total = sum(counts.values()) + overflow
    return (f"RESULT arm={arm} saver={label} samples={total} total_reported={total_reported} "
            f"p50_us={percentile(counts, overflow, .5)} p99_us={percentile(counts, overflow, .99)} "
            f"p999_us={percentile(counts, overflow, .999)} max_us={max_us if max_us is not None else -1} "
            f"overflow_gt20ms={overflow} helix_cpu={fields.get('helix_cpu', '-1')}% "
            f"saver_started={fields.get('saver_started', '-1')} "
            f"saver_stopped_midrun={fields.get('saver_stopped_midrun', '-1')} "
            f"ct_exit={fields.get('ct_exit', '-1')} ct_err={fields.get('ct_err', 'none')}")


def main(argv):
    histogram, arm, label, raw = argv[1:5]
    with open(histogram) as lines:
        print(result_line(lines, arm, label, raw))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
