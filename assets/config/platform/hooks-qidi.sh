#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Platform hooks: QIDI-class SBC (Q2 and siblings)
#
# A QIDI reports platform `pi`, whose hooks are no-ops, so
# resolve_platform_hook_key() selects this file by the same fingerprint that
# vetoes Artillery M1 detection rather than by the platform name.

# The stock screen's two shapes, kept identical to the installer's
# competing_uis module. tests/shell/test_platform_hooks.bats fails if the two
# ever disagree.
QIDI_STOCK_UI_BINS="/home/mks/QD_Q2/bin/client /home/qidi/QD_Q2/bin/client"
QIDI_STOCK_UI_EXEC_PATTERN='QD_Q2/bin/|qidiclient|qidi-client|makerbase-client'

platform_stop_competing_uis() {
    _qidi_path=""
    _qidi_unit=""
    _qidi_bin=""
    _qidi_proc=""

    # Units before processes: the stock screen unit sets Restart=always with
    # StartLimitIntervalSec=0, and its start script runs the client a second
    # time under taskset when the first exits, so a process killed while its
    # unit is still live comes straight back once a second.
    if command -v systemctl >/dev/null 2>&1; then
        for _qidi_path in /etc/systemd/system/*.service /lib/systemd/system/*.service; do
            [ -f "$_qidi_path" ] || continue
            _qidi_unit=$(basename "$_qidi_path")
            case "$_qidi_unit" in ${SERVICE_NAME:-helixscreen}*) continue ;; esac
            grep -E '^ExecStart=' "$_qidi_path" 2>/dev/null \
                | grep -qiE "$QIDI_STOCK_UI_EXEC_PATTERN" || continue
            systemctl stop "$_qidi_unit" 2>/dev/null || true
            systemctl disable "$_qidi_unit" 2>/dev/null || true
        done
    fi

    # A client that outlived its unit holds the framebuffer just as well. Its
    # basename is `client`, too generic for pidof, so match the resolved exe.
    for _qidi_bin in $QIDI_STOCK_UI_BINS; do
        [ -f "$_qidi_bin" ] || continue
        for _qidi_proc in /proc/[0-9]*; do
            [ "$(readlink "$_qidi_proc/exe" 2>/dev/null)" = "$_qidi_bin" ] || continue
            kill "${_qidi_proc#/proc/}" 2>/dev/null || true
        done
    done
}

platform_enable_backlight() {
    :
}

platform_wait_for_services() {
    :
}

platform_pre_start() {
    :
}

platform_post_stop() {
    :
}
