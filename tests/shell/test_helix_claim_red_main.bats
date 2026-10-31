#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# `take worktree:<main tree>` refuses while the latest completed main-CI run on
# origin/main failed, so nobody starts editing on top of a red main without
# knowing. --ack-red-main overrides it. Other trees are unaffected, and an
# unreadable CI state (no gh, gh failing) lets the take proceed with a warning.
# gh is always a mock here; the real one is never called.

load helpers

CLAIM="$(cd "${BATS_TEST_DIRNAME}/../.." && pwd)/scripts/helix-claim"

setup() {
    export HELIX_CLAIM_DIR="$BATS_TEST_TMPDIR/claims"
    MAIN="$BATS_TEST_TMPDIR/mainrepo"
    LINKED="$MAIN/.worktrees/feat"
    mkdir -p "$MAIN" "$BATS_TEST_TMPDIR/bin"
    cd "$MAIN" || return 1
    git init -q -b main .
    git config user.email t@example.com
    git config user.name tester
    echo a > f.txt; git add f.txt; git commit -qm base --no-verify
    git remote add origin https://example.invalid/x.git
    git worktree add -q -b feat "$LINKED"
    export PATH="$BATS_TEST_TMPDIR/bin:$PATH"
    GH_ARGS="$BATS_TEST_TMPDIR/gh.args"
    sleep 120 & OWNER=$!
}

teardown() {
    [ -n "${OWNER:-}" ] && kill "$OWNER" 2>/dev/null
    return 0
}

# Newest first, as gh prints them: workflow<TAB>conclusion<TAB>sha.
mock_gh_runs() {
    printf '%s\n' "$@" > "$BATS_TEST_TMPDIR/gh.out"
    cat > "$BATS_TEST_TMPDIR/bin/gh" <<MOCK
#!/bin/sh
echo "\$*" > "$GH_ARGS"
cat "$BATS_TEST_TMPDIR/gh.out"
MOCK
    chmod +x "$BATS_TEST_TMPDIR/bin/gh"
}

TAB=$'\t'

@test "take worktree:main is refused while the latest Build run on main failed" {
    mock_gh_runs "Build${TAB}failure${TAB}abc123456" "Build${TAB}success${TAB}old000000"
    run "$CLAIM" take worktree:main edit --pid "$OWNER"
    [ "$status" -eq 1 ]
    contains "REFUSED" "$output"
    contains "origin/main is red" "$output"
    contains "Build abc123456" "$output"
    contains "--ack-red-main" "$output"
    contains "--branch main" "$(cat "$GH_ARGS")"
}

@test "--ack-red-main takes the claim anyway" {
    mock_gh_runs "Build${TAB}failure${TAB}abc123456"
    run "$CLAIM" take worktree:main edit --pid "$OWNER" --ack-red-main
    [ "$status" -eq 0 ]
    contains "CLAIMED" "$output"
}

@test "a green latest run lets the take through even after an older failure" {
    mock_gh_runs "Build${TAB}success${TAB}new000000" "Build${TAB}failure${TAB}old000000"
    run "$CLAIM" take worktree:main edit --pid "$OWNER"
    [ "$status" -eq 0 ]
}

@test "a cancelled latest run does not hide the failure under it" {
    mock_gh_runs "Build${TAB}cancelled${TAB}new000000" "Code Quality${TAB}failure${TAB}old000000"
    run "$CLAIM" take worktree:main edit --pid "$OWNER"
    [ "$status" -eq 1 ]
    contains "Code Quality old000000" "$output"
}

@test "a failing workflow outside the main CI set does not block" {
    mock_gh_runs "Nightly Full Test Suite${TAB}failure${TAB}abc123456"
    run "$CLAIM" take worktree:main edit --pid "$OWNER"
    [ "$status" -eq 0 ]
}

@test "a linked worktree is not subject to the check" {
    mock_gh_runs "Build${TAB}failure${TAB}abc123456"
    run "$CLAIM" take worktree:feat edit --pid "$OWNER"
    [ "$status" -eq 0 ]
    [ ! -e "$GH_ARGS" ]
}

@test "a gh that fails lets the take proceed with a one-line warning" {
    printf '#!/bin/sh\nexit 1\n' > "$BATS_TEST_TMPDIR/bin/gh"; chmod +x "$BATS_TEST_TMPDIR/bin/gh"
    run "$CLAIM" take worktree:main edit --pid "$OWNER"
    [ "$status" -eq 0 ]
    contains "skipping the red-main check" "$output"
    contains "CLAIMED" "$output"
}

@test "a repo with no origin skips the check without calling gh" {
    git remote remove origin
    mock_gh_runs "Build${TAB}failure${TAB}abc123456"
    run "$CLAIM" take worktree:main edit --pid "$OWNER"
    [ "$status" -eq 0 ]
    [ ! -e "$GH_ARGS" ]
}

@test "startup_failure counts as red" {
    mock_gh_runs "Build${TAB}startup_failure${TAB}abc123456"
    run "$CLAIM" take worktree:main edit --pid "$OWNER"
    [ "$status" -eq 1 ]
    contains "Build abc123456" "$output"
}

@test "the query is limited to push runs" {
    mock_gh_runs "Build${TAB}success${TAB}abc123456"
    run "$CLAIM" take worktree:main edit --pid "$OWNER"
    [ "$status" -eq 0 ]
    contains "--event push" "$(cat "$GH_ARGS")"
}

@test "a failed run whose commit is not on origin/main (fork PR) is ignored" {
    git update-ref refs/remotes/origin/main HEAD
    git checkout -q -b forkpr
    echo x > x.txt; git add x.txt; git commit -qm fork --no-verify
    forksha=$(git rev-parse HEAD)
    git checkout -q main
    mock_gh_runs "Build${TAB}failure${TAB}${forksha}"
    run "$CLAIM" take worktree:main edit --pid "$OWNER"
    [ "$status" -eq 0 ]
}

@test "a failed run whose commit is on origin/main still blocks" {
    git update-ref refs/remotes/origin/main HEAD
    mainsha=$(git rev-parse HEAD)
    mock_gh_runs "Build${TAB}failure${TAB}${mainsha}"
    run "$CLAIM" take worktree:main edit --pid "$OWNER"
    [ "$status" -eq 1 ]
}

@test "without timeout or gtimeout gh still runs, with no warning" {
    mock_gh_runs "Build${TAB}success${TAB}abc123456"
    # A PATH holding only the mock gh plus the tools the script needs, minus timeout.
    mkdir -p "$BATS_TEST_TMPDIR/nobin"
    for t in git sed awk tr cat grep paste basename dirname date sleep mkdir mv rm head cut ps hostname kill id stat wc sort uniq env bash sh; do
        p=$(command -v "$t") && ln -sf "$p" "$BATS_TEST_TMPDIR/nobin/$t"
    done
    ln -sf "$BATS_TEST_TMPDIR/bin/gh" "$BATS_TEST_TMPDIR/nobin/gh"
    PATH="$BATS_TEST_TMPDIR/nobin" run "$CLAIM" take worktree:main edit --pid "$OWNER"
    [ "$status" -eq 0 ]
    lacks "skipping" "$output"
    [ -e "$GH_ARGS" ]
}

@test "the pre-commit merge claim never asks about CI" {
    grep -q -- '--ack-red-main' "$BATS_TEST_DIRNAME/../../.githooks/lib/claim-tree.sh"
}
