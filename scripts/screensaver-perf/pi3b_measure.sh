#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs ON the Pi through perf_run_remote with TYPE=<settings dropdown index> and optional
# SETTLE_S=<seconds> with no ctl traffic before a saver starts, for a gate idle baseline
# (the last 10 s before a saver runs) that sees the app quiet.
# TYPE=0 measures CPU on the idle home panel. Any other type starts that saver with the Test
# Screensaver button, measures CPU for 20 s and traces ioctls for 10 s; the trace is left at
# /tmp/hm_strace.txt for the host to collect. Prints CPU, STOPPED_MIDRUN and STARTED lines.
set -u
PW=${PW:-}
INSTALL=${INSTALL:?}
CTL_SOCK=${CTL_SOCK:?}
TYPE=${TYPE:-1}
SETTLE_S=${SETTLE_S:-0}
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

# "<tid> <utime+stime>" for every thread of the app.
thread_ticks() {
    local task tid
    for task in /proc/"$PID"/task/*; do
        tid=${task##*/}
        # shellcheck disable=SC2046 # the stat fields are meant to split into arguments
        set -- $(sed 's/^.*) //' "$task/stat" 2>/dev/null)
        [ $# -ge 13 ] && echo "$tid $((${12} + ${13}))"
    done
}

cpu_window() {
    local seconds=$1
    thread_ticks | sort > /tmp/hm_s0
    sleep "$seconds"
    thread_ticks | sort > /tmp/hm_s1
    join /tmp/hm_s0 /tmp/hm_s1 | awk -v hz="$HZ" -v w="$seconds" -v pid="$PID" '
        { d = $3 - $2; tot += d; if ($1 == pid) mainv = d; else if (d > other) other = d }
        END { printf "CPU total=%.1f main=%.1f busiest_other=%.1f\n", (tot/hz)/w*100, (mainv/hz)/w*100, (other/hz)/w*100 }'
    rm -f /tmp/hm_s0 /tmp/hm_s1
}

ORIG_TYPE=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["display"]["screensaver_type"])' "$INSTALL/config/settings.json")
ctl navigate home
sleep 2

if [ "$TYPE" -eq 0 ]; then
    cpu_window 20
    exit 0
fi

ctl navigate settings
sleep 1
ctl click row_display
sleep 1
ctl set_value row_screensaver "$TYPE"
sleep 1
sleep "$SETTLE_S"
T_START=$(date '+%Y-%m-%d %H:%M:%S')
ctl click btn_test_screensaver
sleep 5

cpu_window 20
sudo_ rm -f /tmp/hm_strace.txt
sudo_ timeout 10 strace -f -ttt -e trace=ioctl -o /tmp/hm_strace.txt -p "$PID" 2>/dev/null
echo "STOPPED_MIDRUN $(sudo_ journalctl -u helixscreen --since "$T_START" --no-pager -o cat 2>/dev/null | grep -c 'Stopping')"
echo "STARTED $(sudo_ journalctl -u helixscreen --since "$T_START" --no-pager -o cat 2>/dev/null | grep -c 'Started screensaver type')"

ctl wake
sleep 1
ctl navigate settings
sleep 1
ctl click row_display
sleep 1
ctl set_value row_screensaver "$ORIG_TYPE"
sleep 1
ctl navigate home
