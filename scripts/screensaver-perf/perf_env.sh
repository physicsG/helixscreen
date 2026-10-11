#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Settings shared by the screensaver measurement scripts. Source it from bash; it defines
# the ssh helpers and stops with a message when a required setting is missing.
#
#   HELIX_PERF_HOST      device address (required)
#   HELIX_PERF_SCRATCH   directory for results, logs and copied binaries (required, outside the repo)
#   HELIX_PERF_USER      ssh user (default pi)
#   HELIX_PERF_PASSWORD  ssh and sudo password; unset means key auth and passwordless sudo
#   HELIX_PERF_INSTALL   install root on the device (default /home/pi/helixscreen)
#   HELIX_PERF_CTL_SOCK  helix-screen control socket on the device (default /run/helixscreen/control.sock)

: "${HELIX_PERF_HOST:?set HELIX_PERF_HOST to the device address}"
: "${HELIX_PERF_SCRATCH:?set HELIX_PERF_SCRATCH to a directory outside the repo}"
HELIX_PERF_USER=${HELIX_PERF_USER:-pi}
HELIX_PERF_INSTALL=${HELIX_PERF_INSTALL:-/home/pi/helixscreen}
HELIX_PERF_CTL_SOCK=${HELIX_PERF_CTL_SOCK:-/run/helixscreen/control.sock}

PERF_TARGET="$HELIX_PERF_USER@$HELIX_PERF_HOST"
# shellcheck disable=SC2034 # read by every script that sources this file
PERF_HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck disable=SC2034 # read by arm_measure.sh
PERF_REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$HELIX_PERF_SCRATCH/results"

# Runs an ssh-based tool against the device. A password reaches sshpass through its
# environment, so it never appears in a process list. DISPLAY and SSH_ASKPASS are cleared
# so no desktop password dialog opens.
perf_tool() {
    local tool=$1
    shift
    if [ -n "${HELIX_PERF_PASSWORD:-}" ]; then
        DISPLAY='' SSH_ASKPASS='' SSHPASS="$HELIX_PERF_PASSWORD" sshpass -e "$tool" \
            -o PubkeyAuthentication=no -o PreferredAuthentications=password \
            -o StrictHostKeyChecking=no -o ConnectTimeout=10 "$@"
    else
        DISPLAY='' SSH_ASKPASS='' "$tool" -o BatchMode=yes -o StrictHostKeyChecking=no \
            -o ConnectTimeout=10 "$@"
    fi
}

perf_ssh() {
    perf_tool ssh "$PERF_TARGET" "$@"
}

perf_scp() {
    perf_tool scp -q "$@"
}

# Copies the contents of a local directory into a directory on the device.
perf_rsync_dir() {
    local ssh_cmd="ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10"
    if [ -n "${HELIX_PERF_PASSWORD:-}" ]; then
        DISPLAY='' SSH_ASKPASS='' SSHPASS="$HELIX_PERF_PASSWORD" sshpass -e rsync -az --checksum \
            -e "$ssh_cmd -o PubkeyAuthentication=no -o PreferredAuthentications=password" \
            "$1/" "$PERF_TARGET:$2/"
    else
        rsync -az --checksum -e "$ssh_cmd -o BatchMode=yes" "$1/" "$PERF_TARGET:$2/"
    fi
}

# Runs a device-side script under bash. The password, install root, control socket and
# every KEY=VALUE argument become the script's first lines, so none travels on a command line.
perf_run_remote() {
    local script=$1
    shift
    {
        printf 'PW=%q\nINSTALL=%q\nCTL_SOCK=%q\n' "${HELIX_PERF_PASSWORD:-}" "$HELIX_PERF_INSTALL" \
            "$HELIX_PERF_CTL_SOCK"
        local kv
        for kv in "$@"; do
            printf '%s=%q\n' "${kv%%=*}" "${kv#*=}"
        done
        cat "$script"
    } | perf_ssh "bash -s"
}

# Settings dropdown index of a workload name. idle, off and ui run no saver.
perf_saver_type() {
    case $1 in
    idle | off | ui) echo 0 ;;
    toasters) echo 1 ;;
    starfield) echo 2 ;;
    pipes) echo 3 ;;
    bounce) echo 4 ;;
    fireworks) echo 5 ;;
    *)
        echo "perf_saver_type: unknown workload '$1'" >&2
        return 1
        ;;
    esac
}

# EGL vsync and a 1 ms main-loop floor keep 16 ms frames even. Every device run sets both:
# the app logs neither when it equals the default, so no log shows which pacing is in force,
# and setting a default changes nothing.
# shellcheck disable=SC2034 # read by arm_measure.sh and by the device steps
PERF_PACING_ENV="HELIX_EGL_VSYNC=1 HELIX_LOOP_MIN_SLEEP_MS=1"

# One line with the Pi's temperature, throttle flags and ARM clock. The Pi 3B throttles under
# a long load gate, and a throttled run is not comparable with one that was not.
perf_thermal() {
    perf_ssh 'echo "$(vcgencmd measure_temp) $(vcgencmd get_throttled) arm_clock=$(vcgencmd measure_clock arm | cut -d= -f2)"' \
        < /dev/null 2> /dev/null
}

# True when a Moonraker print state leaves the board free for a load test. "none" means
# nothing answers at the Moonraker address the app is configured with.
perf_print_idle() {
    case $1 in
    standby | ready | complete | cancelled | error | none) return 0 ;;
    *) return 1 ;;
    esac
}

# For a step that drives the Pi 3B directly: claims device:pi3b for the calling session, then
# checks no print is running. Non-zero, holding nothing, when another session has the Pi or a
# print may be running. perf_pi3b_release gives the claim back.
perf_pi3b_take() {
    local claim=$PERF_REPO/scripts/helix-claim state
    if ! "$claim" check device:pi3b > /dev/null 2>&1; then
        echo "device:pi3b is held by another session; not touching the Pi" >&2
        "$claim" list 2> /dev/null | grep -A2 "device:pi3b" >&2
        return 1
    fi
    "$claim" take device:pi3b "${1:?give a reason}" > /dev/null || return 1
    state=$(perf_run_remote "$PERF_HERE/pi3b_service.sh" ACTION=print_state)
    echo "PRINT_STATE $state"
    if ! perf_print_idle "$state"; then
        echo "not touching the Pi: print state '$state'" >&2
        perf_pi3b_release
        return 1
    fi
}

perf_pi3b_release() {
    "$PERF_REPO/scripts/helix-claim" release device:pi3b > /dev/null
}

# Path of the log file the running app holds open, read from its file descriptors so no step
# assumes where a platform's log hook writes; /var/log/messages when the app logs to syslog
# there instead. Prints an empty line when neither is found.
perf_app_log() {
    perf_ssh 'log=$(for p in $(pidof helix-screen); do for f in /proc/$p/fd/*; do readlink "$f"; done; done 2>/dev/null | grep "\.log$" | head -n 1)
if [ -z "$log" ] && grep -q helix-screen /var/log/messages 2>/dev/null; then log=/var/log/messages; fi
echo "$log"' < /dev/null
}
