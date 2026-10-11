# SPDX-License-Identifier: GPL-3.0-or-later
"""Per-arm, per-workload mean [min..max] tables and the load-gate verdict.

usage: summarize.py <results/arm.txt>...

Reads the CPU, FLIPS, STOPPED_MIDRUN, STARTED and gate RESULT lines that pi3b_pass.sh,
pi3b_gate.sh and embedded_gate.sh append.
"""

import re
import statistics
import sys
from collections import defaultdict

# The load gate: in every run, no wake-up of 20 ms or more and a p99 under 5 ms.
MAX_WAKEUP_US = 20000
MAX_P99_US = 5000

LINE = re.compile(r"^(\S+) run=(\d+) (\S+) (CPU|FLIPS|STOPPED_MIDRUN|STARTED|RESULT)\s*(.*)$")
NUMBER = re.compile(r"(\w+)=([-\d.]+%?)")
GATE_KEYS = ("samples", "p50_us", "p99_us", "p999_us", "max_us", "overflow_gt20ms", "helix_cpu")


def numbers(text):
    return {key: float(value.rstrip("%")) for key, value in NUMBER.findall(text)}


def gate_passes(runs):
    """True when every run of one arm and workload passes the load gate."""
    return (len(runs["p99_us"]) > 0
            and all(samples > 0 for samples in runs["samples"])
            and all(wakeup < MAX_WAKEUP_US for wakeup in runs["max_us"])
            and all(overflow == 0 for overflow in runs["overflow_gt20ms"])
            and all(p99 < MAX_P99_US for p99 in runs["p99_us"]))


def load(paths):
    cpu = defaultdict(lambda: defaultdict(list))
    flips = defaultdict(lambda: defaultdict(list))
    gate = defaultdict(lambda: defaultdict(list))
    flags = []
    for path in paths:
        with open(path) as results:
            for raw in results:
                match = LINE.match(raw.strip())
                if not match:
                    continue
                arm, run, workload, kind, rest = match.groups()
                if kind == "CPU":
                    for key, value in numbers(rest).items():
                        cpu[(arm, workload)][key].append(value)
                elif kind == "FLIPS":
                    if "insufficient" in rest:
                        flags.append(f"{arm} run={run} {workload}: too few flips")
                        continue
                    for key, value in numbers(rest).items():
                        flips[(arm, workload)][key].append(value)
                elif kind == "STOPPED_MIDRUN" and rest.strip() != "0":
                    flags.append(f"{arm} run={run} {workload}: saver stopped mid-run ({rest.strip()})")
                elif kind == "STARTED" and rest.strip() == "0":
                    flags.append(f"{arm} run={run} {workload}: saver never started")
                elif kind == "RESULT":
                    label = re.search(r"saver=(\S+)", rest).group(1)
                    values = numbers(rest)
                    for key in GATE_KEYS:
                        if key in values:
                            gate[(arm, label)][key].append(values[key])
                    if values.get("saver_stopped_midrun", 0) > 0:
                        flags.append(f"{arm} run={run} gate {label}: saver stopped mid-run")
                    if values.get("samples", 0) <= 0:
                        flags.append(f"{arm} run={run} gate {label}: no latency samples "
                                     f"(ct_exit={values.get('ct_exit', '?')})")
                    elif values.get("ct_exit", 0) != 0:
                        flags.append(f"{arm} run={run} gate {label}: probe exit {values.get('ct_exit')}")
                    if label not in ("off", "ui") and values.get("saver_started", 1) == 0:
                        flags.append(f"{arm} run={run} gate {label}: saver never started")
    return cpu, flips, gate, flags


def fmt(values, digits=1):
    if not values:
        return "-"
    return f"{statistics.mean(values):.{digits}f} [{min(values):.{digits}f}..{max(values):.{digits}f}] n={len(values)}"


def main(argv):
    cpu, flips, gate, flags = load(argv[1:])
    print("## CPU (% of one core) and presented frames")
    print(f"{'arm':<18} {'workload':<10} {'cpu total':<26} {'cpu main':<26} {'fps':<26} "
          f"{'p50 ms':<8} {'p90 ms':<8} {'stdev':<6}")
    for key in sorted(set(cpu) | set(flips)):
        arm, workload = key
        c, f = cpu.get(key, {}), flips.get(key, {})
        p50 = f"{statistics.mean(f['p50']):.1f}" if f.get("p50") else "-"
        p90 = f"{statistics.mean(f['p90']):.1f}" if f.get("p90") else "-"
        stdev = f"{statistics.mean(f['stdev']):.1f}" if f.get("stdev") else "-"
        print(f"{arm:<18} {workload:<10} {fmt(c.get('total', [])):<26} {fmt(c.get('main', [])):<26} "
              f"{fmt(f.get('fps', [])):<26} {p50:<8} {p90:<8} {stdev:<6}")
    if gate:
        print("\n## Load gate, microseconds")
        print("Verdict: PASS when every run has no wake-up of 20 ms or more and p99 under 5 ms.")
        print(f"{'arm':<18} {'workload':<10} {'p99':<24} {'p99.9':<24} {'max':<26} {'>20ms':<6} "
              f"{'helix cpu':<22} verdict")
        for (arm, label), runs in sorted(gate.items()):
            print(f"{arm:<18} {label:<10} {fmt(runs['p99_us'], 0):<24} {fmt(runs['p999_us'], 0):<24} "
                  f"{fmt(runs['max_us'], 0):<26} {int(sum(runs['overflow_gt20ms'])):<6} "
                  f"{fmt(runs['helix_cpu']):<22} {'PASS' if gate_passes(runs) else 'FAIL'}")
    if flags:
        print("\n## Contaminated runs")
        for flag in flags:
            print(" -", flag)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
