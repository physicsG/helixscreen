#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs ON the Pi through perf_run_remote, after device_lib.sh, with ARM=<label>
# LABEL=<workload> SAVER=<stable saver name, or off for none> UI_LOOP=0|1 DUR=<seconds>.
# Starts the saver (`ctl screensaver <name>`) or cycles the base panels (UI_LOOP=1), loads
# two cores with stress-ng, runs cyclictest for DUR seconds and prints one GATE_RAW line. The
# histogram is left at /tmp/hm_cyclic_<arm>_<label>.txt for the host to collect.
# cyclictest needs root even for SCHED_OTHER; --laptop keeps it from pinning
# /dev/cpu_dma_latency to 0, which would hold the CPU out of idle states and distort the
# latency being measured. -h is perfstats.py HISTOGRAM_US, the histogram ctparse.py reads.
set -u
PW=${PW:-}
INSTALL=${INSTALL:?}
CTL_SOCK=${CTL_SOCK:?}
ARM=${ARM:-unnamed}
LABEL=${LABEL:-off}
SAVER=${SAVER:-off}
UI_LOOP=${UI_LOOP:-0}
DUR=${DUR:-60}

PID=$(helix_pid)
HZ=$(clk_tck)

ctl navigate home
sleep 1
T_START=$(date '+%Y-%m-%d %H:%M:%S')
if [ "$SAVER" != off ]; then
    ctl screensaver "$SAVER"
    sleep 4
fi

NAV=""
if [ "$UI_LOOP" = "1" ]; then
    (
        while :; do
            for panel in home controls filament settings print-select; do
                ctl navigate "$panel"
                sleep 2
            done
        done
    ) < /dev/null &
    NAV=$!
fi

stress-ng --cpu 2 --cpu-method matrixprod --timeout $((DUR + 15))s --quiet < /dev/null &
STRESS=$!
sleep 5

HIST=/tmp/hm_cyclic_${ARM}_${LABEL}.txt
T0=$(cpu_ticks "$PID")
sudo_ nice -n 0 cyclictest --laptop --policy=other -i 1000 -D "${DUR}s" -q -h 20000 > "$HIST" 2> "$HIST.err"
CT_EXIT=$?
T1=$(cpu_ticks "$PID")

kill "$STRESS" 2>/dev/null
wait "$STRESS" 2>/dev/null
if [ -n "$NAV" ]; then
    kill "$NAV" 2>/dev/null
    wait "$NAV" 2>/dev/null
fi
STARTED=$(saver_starts "$T_START")
STOPPED=$(saver_stops "$T_START")
CT_ERR=$(grep -v -i "password" "$HIST.err" | grep -v "cpu_dma_latency" | head -n 1 | tr ' ' '_')
rm -f "$HIST.err"

if [ "$SAVER" != off ]; then
    ctl wake
    sleep 1
fi
ctl navigate home

gate_raw "$HIST" "$CT_EXIT" "$CT_ERR" "$(cpu_pct $((T1 - T0)) "$HZ" "$DUR")" "$STARTED" "$STOPPED"
