# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/land-gate.sh, which records a commit it passed, and by
# pre-push, which skips the quality sweep that pass already ran on that commit.
#
# A stamp is one file named by the full commit SHA. Only an exact match counts:
# a new commit, an amend or a merge made after the gate is a different SHA and
# gets the full sweep.

# land_gate_stamp_dir: where stamps live. The git common dir is one directory
# for every worktree of a clone, so a gate run in a worktree covers the push
# made from the main tree. HELIX_LAND_GATE_STAMP_DIR relocates it.
land_gate_stamp_dir() {
    if [ -n "${HELIX_LAND_GATE_STAMP_DIR:-}" ]; then
        printf '%s\n' "$HELIX_LAND_GATE_STAMP_DIR"
        return 0
    fi
    local common
    common=$(git rev-parse --git-common-dir 2>/dev/null) || return 1
    common=$(cd "$common" && pwd) || return 1
    printf '%s/helix-land-gate-pass\n' "$common"
}

# land_gate_stamp_write SHA: record that make land-gate passed SHA. Stamps
# older than 30 days are pruned; nobody pushes a commit gated that long ago.
land_gate_stamp_write() {
    local dir
    case "$1" in "" | *[!0-9a-f]*) return 1 ;; esac
    dir=$(land_gate_stamp_dir) || return 1
    mkdir -p "$dir" || return 1
    find "$dir" -type f -mtime +30 -delete 2>/dev/null
    printf 'make land-gate PASS at %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" >"$dir/$1"
}

# land_gate_passed SHA: true when make land-gate passed exactly SHA.
land_gate_passed() {
    local dir
    case "$1" in "" | *[!0-9a-f]*) return 1 ;; esac
    dir=$(land_gate_stamp_dir) || return 1
    [ -f "$dir/$1" ]
}
