#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Device half of deploy-common's stop step (mk/cross.mk): piped to `sh -s` over
# ssh before any file is synced, and run directly by
# tests/shell/test_deploy_stop_app.bats.
#
# Stops every running HelixScreen so the sync never races a live process:
#   - systemd units (helixscreen, and the update path/service that restart it)
#     are stopped through systemctl and then confirmed inactive. A unit that
#     will not stop exits 3 BEFORE anything is killed: killing the process of
#     a Restart=always unit only makes systemd respawn it from a half-synced
#     bin/, so no kill is ever a substitute for stopping the unit.
#   - a user unit is stopped and confirmed inactive the same way.
#   - any instance no unit owns (a launcher started by hand, a SysV/BusyBox
#     init script on the K1/AD5M/CC1) is then killed by name, and the script
#     waits up to 10 seconds (kExitWaitTicks) for every one to be gone. A process
#     this user may not signal (a root-owned instance under a non-root deploy)
#     never goes, so the wait ends in a refusal naming it.
#
# Exit: 0 = nothing left running, 3 = a system or user unit could not be
# stopped, 4 = a process outlived the kill.
# POSIX sh only: the K1, AD5M, CC1 and K2 all run BusyBox ash.

kUnits="helixscreen-update.path helixscreen-update.service helixscreen helixscreen-update.path"
kUserUnit="helix-screen"
kProcs="helix-watchdog helix-screen helix-screen-egl helix-splash"
kExitWaitTicks=50  # of 0.2s

# unit_running [--user] UNIT
unit_running() {
    scope=
    if [ "$1" = --user ]; then
        scope=--user
        shift
    fi
    # shellcheck disable=SC2086  # an empty scope must vanish
    state=$(systemctl $scope show -p ActiveState "$1" 2>/dev/null | sed 's/^ActiveState=//')
    case "$state" in
        "" | inactive | failed) return 1 ;;
        *) return 0 ;;
    esac
}

# Root stops units directly. Anyone else needs sudo that cannot prompt (ssh has
# no tty for it to prompt on, so it would fail or hang), or a polkit rule that
# lets this user manage the unit without asking.
stop_unit() {
    if [ "$(id -u)" -eq 0 ]; then
        systemctl stop "$1"
    else
        sudo -n systemctl stop "$1" 2>/dev/null || systemctl --no-ask-password stop "$1"
    fi
}

if command -v systemctl >/dev/null 2>&1; then
    # helixscreen's ExecStopPost re-arms the update path, which is why the path
    # is stopped both before and after the main unit.
    for unit in $kUnits; do
        unit_running "$unit" || continue
        stop_unit "$unit" >/dev/null 2>&1
    done
    for unit in $kUnits; do
        if unit_running "$unit"; then
            echo "deploy-stop: systemd unit $unit is still running and could not be stopped as $(id -un)" >&2
            exit 3
        fi
    done
    systemctl --user stop "$kUserUnit" >/dev/null 2>&1
    if unit_running --user "$kUserUnit"; then
        echo "deploy-stop: systemd user unit $kUserUnit is still running and could not be stopped as $(id -un)" >&2
        exit 3
    fi
fi

# shellcheck disable=SC2086  # kProcs is a word list
killall $kProcs 2>/dev/null
sleep 0.5
# shellcheck disable=SC2086
killall -9 $kProcs 2>/dev/null
ticks=0
# shellcheck disable=SC2086
while pidof $kProcs >/dev/null 2>&1; do
    if [ "$ticks" -ge "$kExitWaitTicks" ]; then
        echo "deploy-stop: still running after the kill, as $(id -un):" >&2
        # shellcheck disable=SC2086
        for pid in $(pidof $kProcs 2>/dev/null); do
            owner=$(stat -c %U "/proc/$pid" 2>/dev/null) || owner="?"
            name=$(cat "/proc/$pid/comm" 2>/dev/null) || name="?"
            echo "deploy-stop:   pid $pid $name (owner $owner)" >&2
        done
        exit 4
    fi
    sleep 0.2
    ticks=$((ticks + 1))
done
exit 0
