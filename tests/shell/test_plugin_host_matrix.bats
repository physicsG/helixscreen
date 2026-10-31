#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The local half of the plugin host harness: scripts/plugin-host-matrix.sh's
# argument handling and its agreement with tests/plugin-host/cell.sh. The
# matrix itself needs docker and runs on the test host
# (scripts/test-host-run.sh plugin-host), never here.

load helpers

REPO_ROOT="$(cd "${BATS_TEST_DIRNAME}/../.." && pwd)"
MATRIX="$REPO_ROOT/scripts/plugin-host-matrix.sh"
CELL="$REPO_ROOT/tests/plugin-host/cell.sh"

setup() {
    mock_command_script docker "printf '%s\\n' \"\$*\" >> '$BATS_TEST_TMPDIR/docker.log'; exit 1"
}

@test "an unknown layout is refused before docker is touched" {
    run "$MATRIX" --layouts "plain nosuch" --out "$BATS_TEST_TMPDIR/out"
    [ "$status" -eq 2 ]
    contains "unknown layout nosuch" "$output"
    [ ! -e "$BATS_TEST_TMPDIR/docker.log" ]
}

@test "an unknown argument prints usage" {
    run "$MATRIX" --frobnicate
    [ "$status" -eq 2 ]
    contains "--vintages" "$output"
}

@test "every layout the matrix runs is one cell.sh lays out" {
    local layouts l
    layouts=$(sed -n 's/^ALL_LAYOUTS="\(.*\)"$/\1/p' "$MATRIX")
    [ -n "$layouts" ] || fail "no ALL_LAYOUTS in $MATRIX"
    for l in $layouts; do
        sed -n '/^layout() {/,/^}/p' "$CELL" | grep -Eq "^ +([a-z0-9|]+\|)?$l(\|[a-z0-9|]+)?\)" \
            || fail "cell.sh has no layout $l"
    done
}

@test "cell.sh is POSIX sh" {
    sh -n "$CELL"
    if command -v shellcheck >/dev/null 2>&1; then
        shellcheck -s sh "$CELL"
    fi
}
