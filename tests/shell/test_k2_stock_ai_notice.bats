#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for print_k2_stock_ai_notice() in main.sh (#1378).
#
# Installing HelixScreen on a K2 stops the stock failure detection loop: the
# launcher hook stops and disables /etc/init.d/app (the procd service that runs
# Creality's Monitor/master-server/app-server detect loop), and the camera
# module hands /dev/video0 to ustreamer. HelixScreen ships its own detector for
# exactly this gap (K2StockDetectionSource, #1378). The notice has to say BOTH
# halves - the stock loop stops, and HelixScreen detection takes over - and
# these tests pin those claims.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"
MAIN_SH="$WORKTREE_ROOT/scripts/lib/installer/main.sh"

setup() {
    load helpers

    # Override the no-op log_warn from helpers.bash so output is capturable.
    # bats runs each test in a subshell, so this redefine is test-scoped.
    log_warn() { printf '[WARN] %s\n' "$*"; }
    export -f log_warn

    # Colour codes are unset in tests so substring matches are easy.
    BOLD=""
    NC=""
    YELLOW=""

    # main.sh installs `trap error_handler ERR` and `trap cleanup_on_success
    # EXIT INT TERM` at source time. The EXIT one replaces the handler bats
    # relies on to report results, so a failed assertion here would tear the
    # runner down before it prints "not ok": the whole file reports as
    # "Executed 0 tests" and a real regression looks like a harness glitch.
    # Restore bats' own handlers after sourcing so assertions fail as assertions.
    local bats_traps
    bats_traps="$(trap -p EXIT INT TERM)"
    unset _HELIX_MAIN_SOURCED
    . "$MAIN_SH"
    trap - ERR EXIT INT TERM
    eval "$bats_traps"
}

@test "print_k2_stock_ai_notice: says stock failure detection is disabled" {
    run print_k2_stock_ai_notice "k2"
    [ "$status" -eq 0 ]
    contains "AI failure detection" "$output"
    # Shouted, because it is the one line that has to survive being skimmed.
    [[ "$output" == *"DISABLED"* ]]
}

@test "print_k2_stock_ai_notice: names both mechanisms that stop it" {
    run print_k2_stock_ai_notice "k2"
    [ "$status" -eq 0 ]
    # The stock UI service that carries the detect loop...
    contains "/etc/init.d/app" "$output"
    # ...and the camera the detector would need.
    [[ "$output" == *"camera"* ]]
}

@test "print_k2_stock_ai_notice: points at uninstall as the way back" {
    run print_k2_stock_ai_notice "k2"
    [ "$status" -eq 0 ]
    [[ "$output" == *"uninstall"* ]]
}

@test "print_k2_stock_ai_notice: says HelixScreen provides its own detection" {
    run print_k2_stock_ai_notice "k2"
    [ "$status" -eq 0 ]
    # The stock loop stops, so the notice must name the detector that replaces
    # it: silence here would leave the owner believing prints are unwatched.
    contains "HelixScreen provides its own" "$output"
    contains "spaghetti detection" "$output"
    # What the built-in detector actually does, so the claim is not vague.
    contains "/usr/bin/detection" "$output"
    contains "pauses the print" "$output"
    # Pausing is a setting seeded from the printer's own choice: warn-only must
    # be visible, and pausePrint named as where the choice starts from.
    contains "only warns" "$output"
    contains "pausePrint" "$output"
    contains "Settings" "$output"
}

@test "print_k2_stock_ai_notice: silent on every other platform" {
    for plat in pi pi32 k1 ad5m ad5x cc1 m1 x86 snapmaker-u1; do
        run print_k2_stock_ai_notice "$plat"
        [ "$status" -eq 0 ]
        [ -z "$output" ]
    done
}

@test "print_k2_stock_ai_notice: silent when no platform is given" {
    run print_k2_stock_ai_notice
    [ "$status" -eq 0 ]
    [ -z "$output" ]
}

@test "install completion calls the notice" {
    # The function is worthless if nothing invokes it. Pin the call site so a
    # refactor of the completion block cannot silently drop it.
    grep -q 'print_k2_stock_ai_notice "\$platform"' "$MAIN_SH"
}
