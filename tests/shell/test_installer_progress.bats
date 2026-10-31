#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# install.sh --progress: the machine-readable HELIX-PROGRESS stream on stdout.
# The step layer is exercised directly; the platform and result lines are
# checked on whole runs of the bundled installer in the e2e sandbox.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"
SCENARIO="$WORKTREE_ROOT/tests/shell/fixtures/install_e2e_scenario.sh"
TAB=$'\t'

setup() {
    load helpers
    unset HELIX_INSTALL_VERBOSE
    export HELIX_INSTALL_TTY=0
    . "$WORKTREE_ROOT/scripts/lib/installer/common.sh"
}

# Lines of a stream whose second field is <kind>.
lines_of() { # kind stream
    printf '%s\n' "$2" | awk -F'\t' -v k="$1" '$1 == "HELIX-PROGRESS" && $2 == k'
}

@test "without --progress the step layer prints nothing on stdout" {
    out=$(step "Working"; step_done "detail" 2>/dev/null)
    [ -z "$out" ]
}

@test "a step emits start then ok with number, total, title and detail" {
    PROGRESS=1
    STEP_TOTAL=3
    out=$(step "Working" "Worked"; step_done "detail" 2>/dev/null)
    expected=$(printf 'HELIX-PROGRESS\tstep\t1\t3\tstart\tWorking\t\nHELIX-PROGRESS\tstep\t1\t3\tok\tWorked\tdetail')
    [ "$out" = "$expected" ]
}

@test "fail and skip each close their step with one terminal status" {
    PROGRESS=1
    STEP_TOTAL=2
    out=$(step "A"; step_fail "boom" 2>/dev/null; step "B"; step_skip 2>/dev/null)
    [ "$(lines_of step "$out" | wc -l)" -eq 4 ]
    lines_of step "$out" | sed -n 2p | grep -q "^HELIX-PROGRESS${TAB}step${TAB}1${TAB}2${TAB}fail${TAB}A${TAB}boom$"
    lines_of step "$out" | sed -n 4p | grep -q "^HELIX-PROGRESS${TAB}step${TAB}2${TAB}2${TAB}skip${TAB}B${TAB}$"
}

@test "opening a step closes a stale open one" {
    PROGRESS=1
    out=$(step "A"; step "B"; step_done 2>/dev/null)
    [ "$(lines_of step "$out" | awk -F'\t' '$5=="start"' | wc -l)" -eq 2 ]
    [ "$(lines_of step "$out" | awk -F'\t' '$5=="ok"' | wc -l)" -eq 2 ]
}

@test "tabs and newlines in a title or detail become single spaces" {
    PROGRESS=1
    out=$(step "Two${TAB}words"; step_done "line one
line two" 2>/dev/null)
    last=$(lines_of step "$out" | tail -1)
    [ "$(printf '%s' "$last" | awk -F'\t' '{print NF}')" -eq 7 ]
    [ "$(printf '%s' "$last" | cut -f6)" = "Two words" ]
    [ "$(printf '%s' "$last" | cut -f7)" = "line one line two" ]
}

@test "a whole run emits one platform line, balanced steps and a final result ok" {
    need_sandbox
    run_progress install-progress
    [ "$(lines_of platform "$stream" | wc -l)" -eq 1 ]
    [ "$(lines_of platform "$stream" | cut -f3)" = "x86" ]
    # Each start is followed, before the next start, by one terminal status for the same n.
    lines_of step "$stream" | awk -F'\t' '
        $5 == "start" { if (open) bad = 1; open = 1; n = $3; nstarts++; next }
        { if (!open || $3 != n) bad = 1; open = 0 }
        END { exit (bad || open || nstarts < 2) }' || fail "unbalanced step events: $stream"
    [ "$(printf '%s\n' "$stream" | grep -c HELIX-PROGRESS)" -gt 3 ]
    [ "$(printf '%s\n' "$stream" | tail -1)" = "$(printf 'HELIX-PROGRESS\tresult\tok')" ]
}

@test "a failing run ends with result fail and a reason" {
    need_sandbox
    run_progress install-progress-fail || true
    last=$(printf '%s\n' "$stream" | tail -1)
    [ "$(printf '%s' "$last" | cut -f1-3)" = "$(printf 'HELIX-PROGRESS\tresult\tfail')" ]
    [ -n "$(printf '%s' "$last" | cut -f4)" ]
    [ "$(printf '%s\n' "$stream" | grep -c "${TAB}result${TAB}")" -eq 1 ]
}

@test "a --desktop run reports the platform it installs as" {
    need_sandbox
    run_progress progress-desktop-dry-run
    [ "$(lines_of platform "$stream" | cut -f3)" = "x86-desktop" ]
}

@test "a --dry-run run ends with result ok" {
    need_sandbox
    run_progress progress-dry-run
    [ "$(printf '%s\n' "$stream" | tail -1)" = "$(printf 'HELIX-PROGRESS\tresult\tok')" ]
}

@test "a bad argument ends with result fail and emits no step or platform line" {
    need_sandbox
    run_progress progress-bad-arg
    [ "$(printf '%s\n' "$stream" | grep -c "^HELIX-PROGRESS${TAB}")" -eq 1 ]
    [ "$(printf '%s\n' "$stream" | tail -1 | cut -f1-3)" = "$(printf 'HELIX-PROGRESS\tresult\tfail')" ]
}

@test "without --progress a whole run prints no HELIX-PROGRESS line" {
    need_sandbox
    run_scenario_raw install
    [[ "$output" != *HELIX-PROGRESS* ]]
}

need_sandbox() {
    command -v unshare >/dev/null 2>&1 || skip "unshare not available"
    [ "$(id -u)" -ne 0 ] || skip "refuses to run as real root"
    unshare --user --map-root-user --mount --pid --net --fork true 2>/dev/null \
        || skip "user+mount+pid+net namespaces not permitted"
    [ -d /mnt ] || skip "no /mnt to reach the work dir through"
    build_sandbox_inputs
}

# The e2e fixtures this file needs: the bundle and one fake release.
build_sandbox_inputs() {
    WORK="$BATS_TEST_TMPDIR/work"
    mkdir -p "$WORK/stubs" "$WORK/seed" "$WORK/out"
    bash "$WORKTREE_ROOT/scripts/bundle-installer.sh" -o "$WORK/install.sh" >/dev/null
    cp /etc/os-release "$WORK/seed/os-release"
    printf 'root:x:0:0:root:/root:/bin/sh\n' > "$WORK/seed/passwd"
    printf 'root:x:0:\n' > "$WORK/seed/group"
    printf '[server]\nhost: 0.0.0.0\n\n[authorization]\n' > "$WORK/seed/moonraker.conf"
    printf '#!/bin/sh\nexit 0\n' > "$WORK/stubs/systemctl"
    chmod +x "$WORK/stubs/systemctl"
    local pkg="$WORK/pkg1/helixscreen" b
    mkdir -p "$pkg/bin" "$pkg/assets" "$pkg/ui_xml" "$WORK/release-1"
    for b in helix-screen helix-splash helix-watchdog; do cp /bin/true "$pkg/bin/$b"; done
    cp "$WORKTREE_ROOT/scripts/helix-launcher.sh" "$pkg/bin/"
    (cd "$WORKTREE_ROOT" && git ls-files -z config | xargs -0 cp --parents -t "$pkg")
    rm -f "$pkg/config/settings.json" "$pkg/config/helixconfig.json"
    printf '{"project_name":"helixscreen","project_owner":"prestonbrown","version":"v1.0.0","asset_name":"helixscreen-x86.zip"}\n' > "$pkg/release_info.json"
    head -c 1600000 /dev/urandom > "$pkg/assets/pad.bin"
    tar -czf "$WORK/release-1/helixscreen-x86-v1.0.0.tar.gz" -C "$WORK/pkg1" helixscreen
}

run_scenario_raw() {
    run unshare --user --map-root-user --mount --pid --net --fork bash "$SCENARIO" "$WORK" "$@"
    if [[ "$output" == *"SANDBOX_MOUNT_FAIL"* ]]; then skip "namespace mounts not permitted here"; fi
}

# Run one scenario step and keep only its stdout stream in $stream.
run_progress() { # step
    run_scenario_raw "$1"
    stream=$(printf '%s\n' "$output" | sed -n "/^=== STEP 1: /,/^=== STEP 1: .* exit=/p" | sed '1d;$d')
    case "$1" in install-progress-fail | progress-bad-arg | progress-desktop-dry-run) return 0 ;; esac
    [ "$status" -eq 0 ] || fail "scenario failed: $output"
}
