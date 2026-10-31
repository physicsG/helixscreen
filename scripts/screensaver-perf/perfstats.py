# SPDX-License-Identifier: GPL-3.0-or-later
"""Statistics the measurement parsers share: one percentile rule and the histogram limit.

ctparse.py reads latency histograms and flips.py lists of frame gaps; both report the same
percentile, the smallest value at which the running count reaches the fraction of samples.
"""

# Top of the latency histogram, in microseconds: cyclictest -h in pi3b_loadgate.sh and
# HISTOGRAM_US in wakeup_probe.c. A wake-up this late or later lands in the overflow count.
HISTOGRAM_US = 20000


def _rank_up(value_counts, total, fraction):
    need = total * fraction
    running = 0
    for value, count in value_counts:
        running += count
        if running >= need:
            return value
    return None


def percentile(sorted_values, fraction):
    """Smallest value holding `fraction` of `sorted_values`; None when there are none."""
    return _rank_up(((value, 1) for value in sorted_values), len(sorted_values), fraction)


def histogram_percentile(counts, overflow, fraction):
    """percentile() over a {bin: count} histogram plus `overflow` samples past its top.

    HISTOGRAM_US when the fraction falls in the overflow, -1 for an empty histogram.
    """
    total = sum(counts.values()) + overflow
    if total == 0:
        return -1
    value = _rank_up(sorted(counts.items()), total, fraction)
    return HISTOGRAM_US if value is None else value
