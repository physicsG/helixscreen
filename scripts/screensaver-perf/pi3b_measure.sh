#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs ON the Pi through perf_run_remote, after device_lib.sh, with SAVER=<stable saver name,
# or off> and optional SETTLE_S=<seconds> with no ctl traffic before a saver starts, for a gate
# idle baseline (the last 10 s before a saver runs) that sees the app quiet.
# SAVER=off measures CPU on the idle home panel. Any other name starts that saver through
# `ctl screensaver <name>`, the Test Screensaver path (a saver too heavy for the board runs its
# fallback), measures CPU for 20 s and traces ioctls for 10 s; the trace is left at
# /tmp/hm_strace.txt for the host to collect. Prints CPU, STOPPED_MIDRUN and STARTED lines.
set -u
PW=${PW:-}
INSTALL=${INSTALL:?}
CTL_SOCK=${CTL_SOCK:?}
SAVER=${SAVER:-toasters}
SETTLE_S=${SETTLE_S:-0}

PID=$(helix_pid)
HZ=$(clk_tck)

# "<tid> <utime+stime>" for every thread of the app. A thread that exits mid-walk prints nothing.
thread_ticks() {
    local task ticks
    for task in /proc/"$PID"/task/*; do
        ticks=$(stat_ticks "$task/stat" 2>/dev/null)
        [ -n "$ticks" ] && echo "${task##*/} $ticks"
    done
}

cpu_window() {
    local seconds=$1
    thread_ticks | sort > /tmp/hm_s0
    sleep "$seconds"
    thread_ticks | sort > /tmp/hm_s1
    # shellcheck disable=SC2046 # three tick counts, meant to split into arguments
    set -- $(join /tmp/hm_s0 /tmp/hm_s1 | awk -v pid="$PID" '
        { d = $3 - $2; tot += d; if ($1 == pid) mainv = d; else if (d > other) other = d }
        END { print tot + 0, mainv + 0, other + 0 }')
    echo "CPU total=$(cpu_pct "$1" "$HZ" "$seconds") main=$(cpu_pct "$2" "$HZ" "$seconds") busiest_other=$(cpu_pct "$3" "$HZ" "$seconds")"
    rm -f /tmp/hm_s0 /tmp/hm_s1
}

ctl navigate home
sleep 2

if [ "$SAVER" = off ]; then
    cpu_window 20
    exit 0
fi

sleep "$SETTLE_S"
T_START=$(date '+%Y-%m-%d %H:%M:%S')
ctl screensaver "$SAVER"
sleep 5

cpu_window 20
sudo_ rm -f /tmp/hm_strace.txt
sudo_ timeout 10 strace -f -ttt -e trace=ioctl -o /tmp/hm_strace.txt -p "$PID" 2>/dev/null
echo "STOPPED_MIDRUN $(saver_stops "$T_START")"
echo "STARTED $(saver_starts "$T_START")"

ctl wake
sleep 1
ctl navigate home
