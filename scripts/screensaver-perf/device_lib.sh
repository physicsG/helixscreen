# shellcheck shell=sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Helpers for the scripts that run ON a device. perf_remote_script in perf_env.sh sends this
# file after the KEY=VALUE prologue and before the script body, because a script read from
# `bash -s` or `sh -s` has nothing beside it to source. POSIX sh, so it also runs under
# BusyBox ash on the embedded boards. PW, INSTALL and CTL_SOCK come from the prologue.

# Runs a command as root, feeding PW to sudo on stdin when one is set.
sudo_() {
    if [ -n "${PW:-}" ]; then
        printf '%s\n' "$PW" | sudo -S -p '' "$@"
    else
        sudo -n "$@"
    fi
}

# `helix-screen ctl` against the device's control socket, printing its reply.
ctl_out() {
    "$INSTALL/bin/helix-screen" ctl -s "$CTL_SOCK" "$@" < /dev/null 2> /dev/null
}

ctl() {
    ctl_out "$@" > /dev/null
}

ctl_answers() {
    ctl_out ping | grep -q pong
}

# PID of the running app, or an empty line. BusyBox ps
# without -o support falls through to pidof.
helix_pid() {
    _pid=$(ps -eo pid,comm 2> /dev/null | awk '$2 ~ /^helix-screen/ {print $1; exit}')
    [ -n "$_pid" ] || _pid=$(pidof helix-screen 2> /dev/null | awk '{print $1}')
    echo "$_pid"
}

# utime + stime, in clock ticks, from a /proc stat file. The command name ends at the last
# ") " and may itself hold spaces, so fields count from there.
stat_ticks() {
    awk '{ sub(/^.*\) /, ""); print $12 + $13 }' "$1"
}

cpu_ticks() {
    stat_ticks "/proc/$1/stat"
}

# Clock ticks per second. BusyBox has no getconf, and its kernels run USER_HZ 100.
clk_tck() {
    _hz=$(getconf CLK_TCK 2> /dev/null)
    echo "${_hz:-100}"
}

# Percent of one core that <ticks> at <hz> ticks per second is over <seconds>.
cpu_pct() {
    awk -v d="$1" -v hz="$2" -v w="$3" 'BEGIN { printf "%.1f", (d / hz) / w * 100 }'
}

# The one GATE_RAW line ctparse.py reads:
#   gate_raw <hist> <ct_exit> <ct_err> <helix_cpu> <saver_started> <saver_stopped_midrun>
# An empty ct_err prints as none.
gate_raw() {
    echo "GATE_RAW hist=$1 ct_exit=$2 ct_err=${3:-none} helix_cpu=$4 saver_started=$5 saver_stopped_midrun=$6"
}

# The app's journal since <since> (a journalctl --since value), one message per line.
app_journal() {
    sudo_ journalctl -u helixscreen --since "$1" --no-pager -o cat 2> /dev/null
}

# How many savers the manager started, and stopped, since <since>. Only the manager's own
# lines count: a saver's "Stopping type N" and any other "Stopping" line do not.
saver_starts() {
    app_journal "$1" | grep -c "Started screensaver type"
}

saver_stops() {
    app_journal "$1" | grep -c "Stopped screensaver type"
}
