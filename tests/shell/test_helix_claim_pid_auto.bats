#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# `helix-claim take ... --pid auto` owns the claim by the nearest ancestor
# whose command name is `claude`, so a session need not walk the process tree
# by hand to find a long-lived owner. With no such ancestor it refuses and says
# what it searched for. The ps(1) path (no /proc) is covered through
# HELIX_CLAIM_PROC pointing at a missing directory.

load helpers

CLAIM="$(cd "${BATS_TEST_DIRNAME}/../.." && pwd)/scripts/helix-claim"

setup() {
    export HELIX_CLAIM_DIR="$BATS_TEST_TMPDIR/claims"
    mkdir -p "$HELIX_CLAIM_DIR" "$BATS_TEST_TMPDIR/fakebin"
    # A shell whose command name is `claude`, standing in for the session.
    ln -s "$(command -v bash)" "$BATS_TEST_TMPDIR/fakebin/claude"
    FAKE_CLAUDE="$BATS_TEST_TMPDIR/fakebin/claude"
}

@test "--pid auto owns the claim by the nearest claude ancestor" {
    run "$FAKE_CLAUDE" -c '
        "$1" take heavy:auto op --pid auto >/dev/null || exit 1
        echo "claude=$$"
        grep "\"pid\"" "$2"' _ "$CLAIM" "$HELIX_CLAIM_DIR/heavy_auto.json"
    [ "$status" -eq 0 ]
    pid=$(sed -n 's/^claude=//p' <<<"$output")
    [ -n "$pid" ]
    contains "\"pid\": $pid," "$output"
}

@test "--pid auto finds the claude ancestor through intermediate shells and without /proc" {
    HELIX_CLAIM_PROC="$BATS_TEST_TMPDIR/no-proc" run "$FAKE_CLAUDE" -c '
        bash -c '"'"'"$1" take heavy:auto op --pid auto >/dev/null; true'"'"' _ "$1" || exit 1
        echo "claude=$$"
        grep "\"pid\"" "$2"' _ "$CLAIM" "$HELIX_CLAIM_DIR/heavy_auto.json"
    [ "$status" -eq 0 ]
    pid=$(sed -n 's/^claude=//p' <<<"$output")
    contains "\"pid\": $pid," "$output"
}

@test "--pid auto with no claude ancestor refuses and names what it looked for" {
    HELIX_CLAIM_SESSION_COMM=no-such-comm run "$CLAIM" take heavy:auto op --pid auto
    [ "$status" -ne 0 ]
    contains "--pid auto" "$output"
    contains "'no-such-comm'" "$output"
    [ ! -e "$HELIX_CLAIM_DIR/heavy_auto.json" ]
}

@test "--pid rejects a value that is neither a number nor auto" {
    run "$CLAIM" take heavy:auto op --pid bogus
    [ "$status" -ne 0 ]
    contains "number or 'auto'" "$output"
    [ ! -e "$HELIX_CLAIM_DIR/heavy_auto.json" ]
}

@test "--session is --pid auto" {
    run "$FAKE_CLAUDE" -c '
        "$1" take heavy:auto op --session >/dev/null || exit 1
        echo "claude=$$"
        grep "\"pid\"" "$2"' _ "$CLAIM" "$HELIX_CLAIM_DIR/heavy_auto.json"
    [ "$status" -eq 0 ]
    pid=$(sed -n 's/^claude=//p' <<<"$output")
    contains "\"pid\": $pid," "$output"
}
