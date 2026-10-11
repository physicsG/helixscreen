#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# screenshot.sh --help lists the HELIX_MOCK_PRINTER ids from the persona table
# in include/mock_persona.h, so the help cannot name a persona the app rejects.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

table_ids() {
    sed -n 's/^ *{"\([a-z0-9_]*\)", PrinterType::.*/\1/p' "$WORKTREE_ROOT/include/mock_persona.h"
}

@test "the persona table yields at least twelve ids" {
    run table_ids
    [ "$status" -eq 0 ]
    [ "$(printf '%s\n' "$output" | wc -l)" -ge 12 ]
}

@test "screenshot.sh --help lists every persona id" {
    run "$WORKTREE_ROOT/scripts/screenshot.sh" --help
    for id in $(table_ids); do
        [[ "$output" == *"$id"* ]] || { echo "missing: $id"; false; }
    done
}
