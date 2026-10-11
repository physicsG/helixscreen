# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the screensaver measurement parsers in scripts/screensaver-perf/."""

import sys
from collections import defaultdict
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts" / "screensaver-perf"))

import ctparse  # noqa: E402
import flips  # noqa: E402
import summarize  # noqa: E402


def fields_of(line):
    return dict(part.split("=", 1) for part in line.split()[1:])


def test_flips_count_page_flip_and_atomic_ioctls_but_not_resumptions():
    log = [
        "812  1726300000.000000 ioctl(10, DRM_IOCTL_MODE_PAGE_FLIP, 0x7ffd) = 0",
        "812  1726300000.016000 ioctl(10, DRM_IOCTL_MODE_ATOMIC, 0x7ffd <unfinished ...>",
        "812  1726300000.017000 <... ioctl resumed>) = 0",
        "812  1726300000.020000 ioctl(10, DRM_IOCTL_MODE_GETRESOURCES, 0x7ffd) = 0",
        "812  1726300000.033000 ioctl(10, DRM_IOCTL_MODE_ATOMIC, 0x7ffd) = 0",
    ]
    assert flips.flip_times(log) == [1726300000.0, 1726300000.016, 1726300000.033]


def test_flips_line_reports_rate_and_largest_gap():
    line = flips.flips_line([0.0, 0.016, 0.033, 0.050])
    fields = fields_of(line)
    assert line.startswith("FLIPS ")
    assert abs(float(fields["fps"]) - 60.0) < 0.1
    assert fields["max"] == "17.0"


def test_flips_line_needs_three_flips():
    assert flips.flips_line([1.0, 2.0]) == "FLIPS n=2 insufficient"


HISTOGRAM = [
    "# Histogram",
    "000100 000090",
    "000200 000009",
    "003000 000001",
    "# Total: 000000100",
    "# Histogram Overflows: 00000",
    "# Max Latencies: 03000",
]


def test_ctparse_reports_percentiles_max_and_cpu():
    fields = fields_of(ctparse.result_line(HISTOGRAM, "armA", "off", "GATE_RAW ct_exit=0 helix_cpu=4.2"))
    assert fields["samples"] == "100"
    assert fields["p50_us"] == "100"
    assert fields["p99_us"] == "200"
    assert fields["p999_us"] == "3000"
    assert fields["max_us"] == "3000"
    assert fields["helix_cpu"] == "4.2%"
    assert fields["ct_exit"] == "0"


def test_ctparse_counts_overflows_in_the_total():
    histogram = ["000050 000010", "# Histogram Overflows: 00002", "# Max Latencies: 25000"]
    fields = fields_of(ctparse.result_line(histogram, "armA", "off", "GATE_RAW ct_exit=0"))
    assert fields["samples"] == "12"
    assert fields["overflow_gt20ms"] == "2"
    assert fields["p999_us"] == "20000"
    assert fields["max_us"] == "25000"


def test_ctparse_empty_histogram_reports_no_samples():
    fields = fields_of(ctparse.result_line([], "armA", "off", "GATE_RAW missing"))
    assert fields["samples"] == "0"
    assert fields["p99_us"] == "-1"
    assert fields["max_us"] == "-1"


def runs(**values):
    table = defaultdict(list)
    for key, series in values.items():
        table[key] = list(series)
    return table


def test_gate_passes_when_every_run_is_under_both_limits():
    assert summarize.gate_passes(
        runs(samples=[60000, 60000], p99_us=[4999, 3000], max_us=[19999, 12000], overflow_gt20ms=[0, 0]))


def test_gate_fails_on_one_wakeup_of_20_ms():
    assert not summarize.gate_passes(
        runs(samples=[60000, 60000], p99_us=[1000, 1000], max_us=[20000, 900], overflow_gt20ms=[0, 0]))


def test_gate_fails_on_one_p99_of_5_ms():
    assert not summarize.gate_passes(
        runs(samples=[60000, 60000], p99_us=[5000, 100], max_us=[9000, 900], overflow_gt20ms=[0, 0]))


def test_gate_fails_on_an_overflow_a_run_without_samples_or_no_runs():
    assert not summarize.gate_passes(
        runs(samples=[60000, 60000], p99_us=[100, 100], max_us=[900, 900], overflow_gt20ms=[0, 1]))
    assert not summarize.gate_passes(
        runs(samples=[60000, 0], p99_us=[100, 100], max_us=[900, 900], overflow_gt20ms=[0, 0]))
    assert not summarize.gate_passes(runs())
