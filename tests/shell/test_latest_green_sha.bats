#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/latest-green-sha.sh in a scratch repo, with a fake gh that answers
# from files: $RUNS/pushes for the push list, $RUNS/<sha> for one commit's runs
# (already in the script's --jq output shape, so nothing here needs a network).

load helpers

setup() {
    SCRIPT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)/scripts/latest-green-sha.sh"
    T="$BATS_TEST_TMPDIR/repo"
    export RUNS="$BATS_TEST_TMPDIR/runs"
    mkdir -p "$T" "$RUNS"
    git -C "$T" init -q
    for n in 1 2 3; do
        git -C "$T" -c user.email=t@t -c user.name=t commit -q --allow-empty -m "c$n"
    done
    C3=$(git -C "$T" rev-parse HEAD) C2=$(git -C "$T" rev-parse HEAD~1) C1=$(git -C "$T" rev-parse HEAD~2)
    git -C "$T" update-ref refs/remotes/origin/main "$C3"
    mock_command_script fake-gh '
c=""; prev=""
for a in "$@"; do [ "$prev" = --commit ] && c=$a; prev=$a; done
if [ -n "$c" ]; then cat "$RUNS/$c" 2>/dev/null; else cat "$RUNS/pushes"; fi'
    export GH=fake-gh
    printf '%s\n' "$C3" "$C2" "$C1" >"$RUNS/pushes"
    cd "$T" || return 1
}

green() { printf 'Build\tcompleted\tsuccess\nCode Quality\tcompleted\tsuccess\n' >"$RUNS/$1"; }

@test "the newest push whose runs all succeeded is the answer" {
    green "$C3"; green "$C2"
    run "$SCRIPT"
    [ "$status" -eq 0 ]
    [ "$output" = "$C3" ]
}

@test "a still-running, failed or cancelled run passes the commit over, and says why" {
    printf 'Build\tin_progress\t\nCode Quality\tcompleted\tsuccess\n' >"$RUNS/$C3"
    printf 'Build\tcompleted\tsuccess\nCode Quality\tcompleted\tfailure\n' >"$RUNS/$C2"
    green "$C1"
    run "$SCRIPT"
    [ "$status" -eq 0 ]
    [ "${lines[-1]}" = "$C1" ]
    contains "Build in_progress" "$output"
    contains "Code Quality failure" "$output"
    printf 'Build\tcompleted\tcancelled\nCode Quality\tcompleted\tsuccess\n' >"$RUNS/$C1"
    run "$SCRIPT"
    [ "$status" -eq 1 ]
    contains "Build cancelled" "$output"
}

@test "a required workflow that never ran is not green; a skipped one is" {
    printf 'Code Quality\tcompleted\tsuccess\n' >"$RUNS/$C3"
    printf 'Build\tcompleted\tsuccess\nCode Quality\tcompleted\tsuccess\nXML Lint\tcompleted\tskipped\n' >"$RUNS/$C2"
    run "$SCRIPT"
    [ "$status" -eq 0 ]
    [ "${lines[-1]}" = "$C2" ]
    contains "Build never ran" "$output"
}

@test "a commit with no runs at all is not green" {
    : >"$RUNS/$C3"
    green "$C2"
    run "$SCRIPT"
    [ "${lines[-1]}" = "$C2" ]
    contains "no CI runs" "$output"
}

@test "a pushed commit no longer on origin/main is skipped" {
    git -c user.email=t@t -c user.name=t commit -q --allow-empty -m gone
    gone=$(git rev-parse HEAD)
    printf '%s\n' "$gone" "$C3" >"$RUNS/pushes"
    green "$gone"; green "$C3"
    run "$SCRIPT"
    [ "$status" -eq 0 ]
    [ "${lines[-1]}" = "$C3" ]
    contains "not on origin/main" "$output"
}

@test "no green commit in the window exits 1" {
    run "$SCRIPT"
    [ "$status" -eq 1 ]
    contains "none of the last 30 pushes to main" "$output"
}

@test "a required workflow that only skipped is not green" {
    printf 'Build\tcompleted\tskipped\t11\nCode Quality\tcompleted\tsuccess\t12\n' >"$RUNS/$C3"
    green "$C2"
    run "$SCRIPT"
    [ "$status" -eq 0 ]
    [ "${lines[-1]}" = "$C2" ]
    contains "Build skipped" "$output"
}

@test "a newest push with only cancelled runs asks for a rerun instead of walking back" {
    printf 'Build\tcompleted\tcancelled\t4242\nCode Quality\tcompleted\tsuccess\t4243\n' >"$RUNS/$C3"
    green "$C2"
    run "$SCRIPT"
    [ "$status" -eq 3 ]
    contains "gh run rerun 4242" "$output"
    lacks "$C2" "$output"
    run "$SCRIPT" --walk-past-cancelled
    [ "$status" -eq 0 ]
    [ "${lines[-1]}" = "$C2" ]
}
