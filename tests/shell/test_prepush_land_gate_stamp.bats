#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# pre-push trusting a make land-gate PASS (.githooks/lib/land-gate-stamp.sh).
# The hook runs against a fixture repo whose gates are stand-ins that each
# print their name, so a test reads which ones a push ran.

load helpers

HOOK="$BATS_TEST_DIRNAME/../../.githooks/pre-push"
STAMP_LIB="$BATS_TEST_DIRNAME/../../.githooks/lib/land-gate-stamp.sh"

setup() {
    export HELIX_LAND_GATE_STAMP_DIR="$BATS_TEST_TMPDIR/stamps"
    unset HELIX_PREPUSH_FULL HELIX_PREPUSH_IN_PLACE
    ROOT="$BATS_TEST_TMPDIR/repo"
    git init -q -b main "$ROOT"
    git -C "$ROOT" config user.email t@t
    git -C "$ROOT" config user.name t
    mkdir -p "$ROOT/scripts" "$ROOT/lib"
    printf '#!/bin/sh\necho "ran: quality-checks"\n' >"$ROOT/scripts/quality-checks.sh"
    chmod +x "$ROOT/scripts/quality-checks.sh"
    local gate
    for gate in check_clang_diagnostics esp32_syntax_check check_patch_drift; do
        printf 'print("ran: %s")\n' "$gate" >"$ROOT/scripts/$gate.py"
    done
    touch "$ROOT/lib/.keep"
    echo base >"$ROOT/f"
    git -C "$ROOT" add -A
    git -C "$ROOT" commit -qm base
    BASE=$(git -C "$ROOT" rev-parse HEAD)
    echo tip >"$ROOT/f"
    git -C "$ROOT" commit -qam tip
    TIP=$(git -C "$ROOT" rev-parse HEAD)
}

stamp() { # <sha>: what make land-gate records on a PASS
    (cd "$ROOT" && . "$STAMP_LIB" && land_gate_stamp_write "$1")
}

run_hook() { # push TIP to a remote that has BASE
    run env -u GIT_DIR -u GIT_WORK_TREE -u GIT_INDEX_FILE -u GIT_OBJECT_DIRECTORY \
        bash -c 'cd "$1" && printf "refs/heads/main %s refs/heads/main %s\n" "$2" "$3" | bash "$4"' \
        _ "$ROOT" "$TIP" "$BASE" "$HOOK"
}

@test "a tip the land-gate passed skips the sweep and still runs ESP32 and drift" {
    stamp "$TIP"
    run_hook
    [ "$status" -eq 0 ]
    contains "make land-gate passed ${TIP:0:9}" "$output"
    lacks "ran: quality-checks" "$output"
    contains "ran: esp32_syntax_check" "$output"
    contains "ran: check_patch_drift" "$output"
}

@test "from a dirty tree, a gated tip skips the sweep and the clang gate" {
    stamp "$TIP"
    echo in-flight >"$ROOT/f"
    run_hook
    [ "$status" -eq 0 ]
    contains "throwaway checkout" "$output"
    contains "make land-gate passed ${TIP:0:9}" "$output"
    lacks "ran: quality-checks" "$output"
    lacks "ran: check_clang_diagnostics" "$output"
    contains "ran: esp32_syntax_check" "$output"
    contains "ran: check_patch_drift" "$output"
}

@test "a stamp for another commit skips nothing" {
    stamp "$BASE"
    run_hook
    [ "$status" -eq 0 ]
    lacks "land-gate passed" "$output"
    contains "ran: quality-checks" "$output"
    echo in-flight >"$ROOT/f"
    run_hook
    [ "$status" -eq 0 ]
    contains "ran: quality-checks" "$output"
    contains "ran: check_clang_diagnostics" "$output"
}

@test "HELIX_PREPUSH_FULL=1 sweeps a gated tip anyway" {
    stamp "$TIP"
    HELIX_PREPUSH_FULL=1 run_hook
    [ "$status" -eq 0 ]
    lacks "land-gate passed" "$output"
    contains "ran: quality-checks" "$output"
}

@test "a stamp written in a linked worktree covers the push from the main tree" {
    unset HELIX_LAND_GATE_STAMP_DIR
    git -C "$ROOT" worktree add -q --detach "$BATS_TEST_TMPDIR/wt" "$TIP"
    (cd "$BATS_TEST_TMPDIR/wt" && . "$STAMP_LIB" && land_gate_stamp_write "$TIP")
    [ -f "$ROOT/.git/helix-land-gate-pass/$TIP" ]
    (cd "$ROOT" && . "$STAMP_LIB" && land_gate_passed "$TIP")
    run bash -c 'cd "$1" && . "$2" && land_gate_passed "$3"' _ "$ROOT" "$STAMP_LIB" "$BASE"
    [ "$status" -ne 0 ]
}

@test "only a full lowercase hex SHA names a stamp" {
    stamp "$TIP"
    run bash -c '. "$1" && land_gate_passed ""' _ "$STAMP_LIB"
    [ "$status" -ne 0 ]
    run bash -c '. "$1" && land_gate_stamp_write "../escape"' _ "$STAMP_LIB"
    [ "$status" -ne 0 ]
    [ ! -e "$BATS_TEST_TMPDIR/escape" ]
}
