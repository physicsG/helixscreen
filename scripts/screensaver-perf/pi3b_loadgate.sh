#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs ON the Pi through perf_run_remote with ARM=<label> LABEL=<workload> TYPE=<dropdown
# index, 0 for none> UI_LOOP=0|1 DUR=<seconds>.
# Starts the saver (TYPE > 0) or cycles the base panels (UI_LOOP=1), loads two cores with
# stress-ng, runs cyclictest for DUR seconds and prints one GATE_RAW line. The histogram is
# left at /tmp/hm_cyclic_<arm>_<label>.txt for the host to collect.
# cyclictest needs root even for SCHED_OTHER; --laptop keeps it from pinning
# /dev/cpu_dma_latency to 0, which would hold the CPU out of idle states and distort the
# latency being measured.
set -u
PW=${PW:-}
INSTALL=${INSTALL:?}
CTL_SOCK=${CTL_SOCK:?}
ARM=${ARM:-unnamed}
LABEL=${LABEL:-off}
TYPE=${TYPE:-0}
UI_LOOP=${UI_LOOP:-0}
DUR=${DUR:-60}
CTL_BIN="$INSTALL/bin/helix-screen-egl"

ctl() {
    "$CTL_BIN" ctl -s "$CTL_SOCK" "$@" < /dev/null > /dev/null 2>&1
}

sudo_() {
    if [ -n "$PW" ]; then
        printf '%s\n' "$PW" | sudo -S -p '' "$@"
    else
        sudo -n "$@"
    fi
}

PID=$(ps -eo pid,comm | awk '$2 ~ /^helix-screen/ {print $1; exit}')
HZ=$(getconf CLK_TCK)

cpu_ticks() {
    # shellcheck disable=SC2046 # the stat fields are meant to split into arguments
    set -- $(sed 's/^.*) //' /proc/"$PID"/stat)
    echo $((${12} + ${13}))
}

ORIG_TYPE=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["display"]["screensaver_type"])' "$INSTALL/config/settings.json")
ctl navigate home
sleep 1
T_START=$(date '+%Y-%m-%d %H:%M:%S')
if [ "$TYPE" -ne 0 ]; then
    ctl navigate settings
    sleep 1
    ctl click row_display
    sleep 1
    ctl set_value row_screensaver "$TYPE"
    sleep 1
    ctl click btn_test_screensaver
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
T0=$(cpu_ticks)
sudo_ nice -n 0 cyclictest --laptop --policy=other -i 1000 -D "${DUR}s" -q -h 20000 > "$HIST" 2> "$HIST.err"
CT_EXIT=$?
T1=$(cpu_ticks)

kill "$STRESS" 2>/dev/null
wait "$STRESS" 2>/dev/null
if [ -n "$NAV" ]; then
    kill "$NAV" 2>/dev/null
    wait "$NAV" 2>/dev/null
fi
STARTED=$(sudo_ journalctl -u helixscreen --since "$T_START" --no-pager -o cat 2>/dev/null | grep -c "Started screensaver type")
STOPPED=$(sudo_ journalctl -u helixscreen --since "$T_START" --no-pager -o cat 2>/dev/null | grep -c "Stopped screensaver type")
CT_ERR=$(grep -v -i "password" "$HIST.err" | grep -v "cpu_dma_latency" | head -n 1 | tr ' ' '_')
rm -f "$HIST.err"

if [ "$TYPE" -ne 0 ]; then
    ctl wake
    sleep 1
    ctl navigate settings
    sleep 1
    ctl click row_display
    sleep 1
    ctl set_value row_screensaver "$ORIG_TYPE"
    sleep 1
fi
ctl navigate home

CPU=$(awk -v d=$((T1 - T0)) -v hz="$HZ" -v w="$DUR" 'BEGIN { printf "%.1f", (d/hz)/w*100 }')
echo "GATE_RAW hist=$HIST ct_exit=$CT_EXIT ct_err=${CT_ERR:-none} helix_cpu=$CPU saver_started=$STARTED saver_stopped_midrun=$STOPPED"
