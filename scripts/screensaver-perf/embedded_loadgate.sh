#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs ON a BusyBox board (CC1, AD5M) through embedded_gate.sh, after device_lib.sh, with
# PROBE=<probe path> HIST=<histogram path> DUR=<seconds>.
# Loads half the cores with busy loops, runs the wake-up probe for DUR seconds and prints one
# GATE_RAW line. The boards keep no journal, so the saver start and stop counts read -1.
PROBE=${PROBE:?}
HIST=${HIST:?}
DUR=${DUR:-60}
HZ=$(clk_tck)

PID=$(helix_pid)
if [ -z "$PID" ]; then
    gate_raw "$HIST" 3 no_helix_screen -1 -1 -1
    exit 0
fi

CORES=$(grep -c '^processor' /proc/cpuinfo)
LOADERS=$(((CORES + 1) / 2))
BUSY=""
i=0
while [ "$i" -lt "$LOADERS" ]; do
    sh -c 'while :; do :; done' < /dev/null &
    BUSY="$BUSY $!"
    i=$((i + 1))
done
sleep 5

T0=$(cpu_ticks "$PID")
"$PROBE" "$DUR" > "$HIST" 2> "$HIST.err"
CT_EXIT=$?
T1=$(cpu_ticks "$PID")

for p in $BUSY; do
    kill "$p" 2>/dev/null
done
CT_ERR=$(head -n 1 "$HIST.err" | tr ' ' '_')
rm -f "$HIST.err"
gate_raw "$HIST" "$CT_EXIT" "$CT_ERR" "$(cpu_pct $((T1 - T0)) "$HZ" "$DUR")" -1 -1
