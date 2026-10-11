#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# detect_competing_uis feeds the plan's Disable line and the summary's
# Disabled row, so it lists a stock UI only while it runs or would start at
# boot. Each platform's disable leaves its files in place (chmod a-x, a
# commented auto_run.sh line, a drop-in), and a re-run on a printer
# HelixScreen already owns must not claim to disable them again.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

setup() {
    load helpers

    export MOCK_ROOT="$BATS_TEST_TMPDIR/root"
    mkdir -p "$MOCK_ROOT/etc/init.d" "$MOCK_ROOT/opt/PROGRAM" \
             "$MOCK_ROOT/home/sovol/printer_data/build" "$MOCK_ROOT/home/mks/QD_Q2/bin" \
             "$MOCK_ROOT/etc/systemd/system" "$MOCK_ROOT/lib/systemd/system"

    INIT_SYSTEM=sysv AD5M_FIRMWARE="" K1_FIRMWARE="" platform=""
    PREVIOUS_UI_SCRIPT="" SERVICE_NAME=helixscreen HOST_OWNS_COMPETING_UIS=""
    mock_command_script pidof 'exit 1'
    _is_self_update() { return 1; }

    # The production module with its absolute paths redirected into MOCK_ROOT.
    local patched="$BATS_TEST_TMPDIR/competing_uis.sh"
    sed -e "s|/etc/init.d/|$MOCK_ROOT/etc/init.d/|g" \
        -e "s|/opt/|$MOCK_ROOT/opt/|g" \
        -e "s|/home/|$MOCK_ROOT/home/|g" \
        -e "s|/etc/systemd/system/|$MOCK_ROOT/etc/systemd/system/|g" \
        -e "s| /lib/systemd/system/| $MOCK_ROOT/lib/systemd/system/|g" \
        "$WORKTREE_ROOT/scripts/lib/installer/competing_uis.sh" > "$patched"
    unset _HELIX_COMPETING_UIS_SOURCED
    # shellcheck disable=SC1090
    . "$patched"
}

_script() { # path mode
    printf '#!/bin/sh\nexit 0\n' > "$1"
    chmod "$2" "$1"
}

@test "k1: a de-executed S99start_app with nothing running is not listed" {
    K1_FIRMWARE=stock_klipper
    _script "$MOCK_ROOT/etc/init.d/S99start_app" 644
    detect_competing_uis
    [ -z "$COMPETING_UIS_FOUND" ]
}

@test "k1: an executable S99start_app is listed" {
    K1_FIRMWARE=stock_klipper
    _script "$MOCK_ROOT/etc/init.d/S99start_app" 755
    detect_competing_uis
    [ "$COMPETING_UIS_FOUND" = Creality-UI ]
}

@test "ad5m: a commented-out ffstartup-arm with nothing running is not listed" {
    _script "$MOCK_ROOT/opt/PROGRAM/ffstartup-arm" 755
    printf '# Disabled by HelixScreen: /opt/PROGRAM/ffstartup-arm &\n' > "$MOCK_ROOT/opt/auto_run.sh"
    detect_competing_uis
    [ -z "$COMPETING_UIS_FOUND" ]
}

@test "ad5m: an auto_run.sh that starts ffstartup-arm is listed" {
    _script "$MOCK_ROOT/opt/PROGRAM/ffstartup-arm" 755
    printf '%s/opt/PROGRAM/ffstartup-arm &\n' "$MOCK_ROOT" > "$MOCK_ROOT/opt/auto_run.sh"
    detect_competing_uis
    [ "$COMPETING_UIS_FOUND" = FlashForge-UI ]
}

@test "ad5m: a running firmwareExe is listed" {
    mock_command_script pidof '[ "$1" = firmwareExe ] && { echo 42; exit 0; }; exit 1'
    detect_competing_uis
    [ "$COMPETING_UIS_FOUND" = FlashForge-UI ]
}

@test "sovol: a de-executed mksclient that is not running is not listed" {
    _script "$MOCK_ROOT/home/sovol/printer_data/build/mksclient" 644
    detect_competing_uis
    [ -z "$COMPETING_UIS_FOUND" ]
}

@test "sovol: an executable mksclient is listed" {
    _script "$MOCK_ROOT/home/sovol/printer_data/build/mksclient" 755
    detect_competing_uis
    [ "$COMPETING_UIS_FOUND" = mksclient ]
}

@test "qidi: a de-executed client binary is not listed" {
    _script "$MOCK_ROOT/home/mks/QD_Q2/bin/client" 644
    detect_competing_uis
    [ -z "$COMPETING_UIS_FOUND" ]
}

@test "qidi: an executable client binary is listed" {
    _script "$MOCK_ROOT/home/mks/QD_Q2/bin/client" 755
    detect_competing_uis
    [ "$COMPETING_UIS_FOUND" = QIDI-UI ]
}

@test "qidi: a stock screen unit listed only while it is active or enabled" {
    printf '[Service]\nExecStart=/home/mks/QD_Q2/bin/client\n' \
        > "$MOCK_ROOT/etc/systemd/system/makerbase-client.service"
    mock_command_script systemctl 'case "$1" in is-enabled) echo disabled ;; esac; exit 1'
    detect_competing_uis
    [ -z "$COMPETING_UIS_FOUND" ]
    mock_command_script systemctl 'case "$1" in is-enabled) echo enabled; exit 0 ;; esac; exit 1'
    detect_competing_uis
    [ "$COMPETING_UIS_FOUND" = makerbase-client ]
}
