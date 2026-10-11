#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs ON a BusyBox board (CC1, AD5M) through embedded_gate.sh with PROBE=<probe path>
# HIST=<histogram path> DUR=<seconds> as its first lines.
# Loads half the cores with busy loops, runs the wake-up probe for DUR seconds and prints one
# GATE_RAW line in the fields ctparse.py reads. USER_HZ is 100 on these kernels, and BusyBox
# has no getconf.
PROBE=${PROBE:?}
HIST=${HIST:?}
DUR=${DUR:-60}
HZ=100

PID=$(pidof helix-screen | awk '{print $1}')
if [ -z "$PID" ]; then
    echo "GATE_RAW hist=$HIST ct_exit=3 ct_err=no_helix_screen helix_cpu=-1 saver_started=-1 saver_stopped_midrun=-1"
    exit 0
fi

cpu_ticks() {
    awk '{ sub(/^.*\) /, ""); print $12 + $13 }' "/proc/$PID/stat"
}

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

T0=$(cpu_ticks)
"$PROBE" "$DUR" > "$HIST" 2> "$HIST.err"
CT_EXIT=$?
T1=$(cpu_ticks)

for p in $BUSY; do
    kill "$p" 2>/dev/null
done
CT_ERR=$(head -n 1 "$HIST.err" | tr ' ' '_')
rm -f "$HIST.err"
CPU=$(awk -v d=$((T1 - T0)) -v hz="$HZ" -v w="$DUR" 'BEGIN { printf "%.1f", (d / hz) / w * 100 }')
echo "GATE_RAW hist=$HIST ct_exit=$CT_EXIT ct_err=${CT_ERR:-none} helix_cpu=$CPU saver_started=-1 saver_stopped_midrun=-1"
