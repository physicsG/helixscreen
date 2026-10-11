#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The claim store belongs to the repository, not to whichever worktree is asking.
#
# A session asking "is main free?" is almost never standing in main, so a store
# anchored per-worktree would answer FREE for a tree another session is holding,
# and would let a second `take` succeed on top of the first. An advisory lock is
# allowed to be ignored; it is not allowed to fail open.

load helpers

CLAIM="$(cd "${BATS_TEST_DIRNAME}/../.." && pwd)/scripts/helix-claim"

setup() {
    unset HELIX_CLAIM_DIR
    MAIN="$BATS_TEST_TMPDIR/mainrepo"
    LINKED="$MAIN/.worktrees/feat"
    mkdir -p "$MAIN"
    cd "$MAIN" || return 1
    git init -q .
    git config user.email t@example.com
    git config user.name tester
    echo a > f.txt
    git add f.txt
    git commit -qm base --no-verify
    git worktree add -q -b feat "$LINKED"
    # An owner that outlives the test body, so the claim reads LIVE throughout.
    sleep 120 &
    OWNER=$!
}

teardown() {
    [ -n "${OWNER:-}" ] && kill "$OWNER" 2>/dev/null
    return 0
}

@test "a claim taken in the main tree is LIVE from a linked worktree" {
    "$CLAIM" take worktree:mainrepo "held by main" --pid "$OWNER" >/dev/null
    cd "$LINKED"
    run "$CLAIM" check worktree:mainrepo
    [ "$status" -eq 1 ]
    contains "held by main" "$output"
}

@test "take from a linked worktree refuses a claim the main tree holds" {
    "$CLAIM" take worktree:mainrepo "held by main" --pid "$OWNER" >/dev/null
    cd "$LINKED"
    run "$CLAIM" take worktree:mainrepo "second session"
    [ "$status" -ne 0 ]
    contains "REFUSED" "$output"
}

@test "a claim taken in a linked worktree is LIVE from the main tree" {
    cd "$LINKED"
    "$CLAIM" take worktree:feat "held by the worktree" --pid "$OWNER" >/dev/null
    cd "$MAIN"
    run "$CLAIM" check worktree:feat
    [ "$status" -eq 1 ]
    contains "held by the worktree" "$output"
}

@test "one store serves the whole repository" {
    "$CLAIM" take worktree:mainrepo "held" --pid "$OWNER" >/dev/null
    cd "$LINKED"
    "$CLAIM" take build:feat "a build" --pid "$OWNER" >/dev/null
    run bash -c "find '$MAIN' -name .helix-claims -type d | wc -l"
    [ "$output" -eq 1 ]
}

@test "list from a linked worktree sees the main tree's claims" {
    "$CLAIM" take worktree:mainrepo "held by main" --pid "$OWNER" >/dev/null
    cd "$LINKED"
    run "$CLAIM" list
    contains "held by main" "$output"
}

@test "HELIX_CLAIM_DIR still overrides the repository store" {
    export HELIX_CLAIM_DIR="$BATS_TEST_TMPDIR/elsewhere"
    "$CLAIM" take worktree:mainrepo "held" --pid "$OWNER" >/dev/null
    [ -d "$HELIX_CLAIM_DIR" ]
    run bash -c "find '$MAIN' -name .helix-claims -type d | wc -l"
    [ "$output" -eq 0 ]
}

# A failed take is loud; an unconditional release is silent on both sides. It hands
# the lock to whoever asked and never tells the holder, so release refuses a claim a
# different live process holds.

@test "release refuses a claim another live session holds" {
    "$CLAIM" take worktree:mainrepo "held by another" --pid "$OWNER" >/dev/null
    run "$CLAIM" release worktree:mainrepo
    [ "$status" -eq 1 ]
    contains "REFUSED" "$output"
    run "$CLAIM" check worktree:mainrepo
    [ "$status" -eq 1 ]
}

@test "release --force drops a claim another live session holds" {
    "$CLAIM" take worktree:mainrepo "held by another" --pid "$OWNER" >/dev/null
    run "$CLAIM" release --force worktree:mainrepo
    [ "$status" -eq 0 ]
    run "$CLAIM" check worktree:mainrepo
    [ "$status" -eq 0 ]
}

@test "release still clears a stale claim, which teardown-worktree depends on" {
    sleep 60 &
    dead=$!
    "$CLAIM" take worktree:mainrepo "dead owner" --pid "$dead" >/dev/null
    kill "$dead" 2>/dev/null
    wait "$dead" 2>/dev/null || true
    run "$CLAIM" release worktree:mainrepo
    [ "$status" -eq 0 ]
    contains "RELEASED" "$output"
}

@test "release drops a claim this session took" {
    "$CLAIM" take worktree:mainrepo "mine" >/dev/null
    run "$CLAIM" release worktree:mainrepo
    [ "$status" -eq 0 ]
    contains "RELEASED" "$output"
}

# Outside a Claude session there is no `claude` ancestor to anchor the owner to,
# so the owner is derived from the caller's own process. A run that takes in one
# shell and releases from a child shell must still recognise its own claim: bats
# `run`, a wrapper script and a CI step all have that shape, and an owner that
# differs between the two leaves a LIVE claim nobody can release.
#
# The shape needs all three of: no `claude` ancestor (the detached launcher exits,
# forks so its child reparents to init), the taker's parent still alive at release time,
# and a release whose own parent differs. The trailing `true` supplies the last
# one: `bash -c` with a single command execs it, keeping the parent unchanged.
@test "a run with no claude ancestor releases a claim it took from a child shell" {
    local out="$BATS_TEST_TMPDIR/detached.out"
    setsid --fork bash -c "cd '$MAIN'
        sleep 0.5
        '$CLAIM' take worktree:mainrepo mine
        bash -c \"'$CLAIM' release worktree:mainrepo; true\"
        '$CLAIM' check worktree:mainrepo" > "$out" 2>&1 &
    local i
    for i in $(seq 200); do
        [ -f "$out" ] && grep -qE "FREE|LIVE" "$out" 2>/dev/null && break
        sleep 0.05
    done
    contains "RELEASED" "$(cat "$out")"
    contains "FREE" "$(cat "$out")"
}

# ---------------------------------------------------------------------------
# build: and worktree: name the same tree
#
# Two prefixes, one directory. Storing them under separate files let one
# session hold build: while editing and another take worktree: and read FREE -
# the fail-open this tool exists to prevent, with only git's own "local changes
# would be overwritten" left to catch it.
# ---------------------------------------------------------------------------

@test "worktree: is refused while another session builds the same tree" {
    "$CLAIM" take build:mainrepo "peer build" --pid "$OWNER" >/dev/null
    run "$CLAIM" take worktree:mainrepo "my merge" --pid 1
    [ "$status" -ne 0 ]
    contains "REFUSED" "$output"
    contains "peer build" "$output"
}

@test "build: is refused while another session writes the same tree" {
    "$CLAIM" take worktree:mainrepo "peer merge" --pid "$OWNER" >/dev/null
    run "$CLAIM" take build:mainrepo "my build" --pid 1
    [ "$status" -ne 0 ]
    contains "REFUSED" "$output"
    contains "peer merge" "$output"
}

@test "one session may hold both build: and worktree: on its own tree" {
    "$CLAIM" take build:mainrepo "my build" --pid "$OWNER" >/dev/null
    run "$CLAIM" take worktree:mainrepo "my merge" --pid "$OWNER"
    [ "$status" -eq 0 ]
    contains "CLAIMED" "$output"
}

@test "check reports a sibling holder rather than answering FREE" {
    "$CLAIM" take build:mainrepo "peer build" --pid "$OWNER" >/dev/null
    run "$CLAIM" check worktree:mainrepo
    [ "$status" -eq 1 ]
    contains "peer build" "$output"
}

@test "a dead sibling owner does not block" {
    # A claim whose owner has exited reads STALE and must not hold the tree.
    sleep 120 &
    dead=$!
    "$CLAIM" take build:mainrepo "abandoned build" --pid "$dead" >/dev/null
    kill "$dead" 2>/dev/null
    wait "$dead" 2>/dev/null || true
    run "$CLAIM" take worktree:mainrepo "my merge" --pid "$OWNER"
    [ "$status" -eq 0 ]
    contains "CLAIMED" "$output"
}

@test "a non-tree resource is unaffected by tree siblings" {
    "$CLAIM" take build:mainrepo "peer build" --pid "$OWNER" >/dev/null
    run "$CLAIM" take device:k2plus "hw verify" --pid 1
    [ "$status" -eq 0 ]
    contains "CLAIMED" "$output"
}

@test "a claim records the address of the session holding it" {
    CLAUDE_CODE_SESSION_ID=abc-123 \
    CLAUDE_CODE_MESSAGING_SOCKET=/run/user/1000/cc-socks/4242.sock \
        "$CLAIM" take worktree:mainrepo "held" --pid "$OWNER" >/dev/null
    run "$CLAIM" check worktree:mainrepo
    [ "$status" -eq 1 ]
    contains "message=uds:/run/user/1000/cc-socks/4242.sock" "$output"
    contains "session_id=abc-123" "$output"
}

@test "an agent outside a Claude session supplies its own address" {
    HELIX_SESSION_ID=opencode-7 HELIX_SESSION_SOCKET=/tmp/agent.sock \
        "$CLAIM" take worktree:mainrepo "held" --pid "$OWNER" >/dev/null
    run "$CLAIM" check worktree:mainrepo
    contains "message=uds:/tmp/agent.sock" "$output"
    contains "session_id=opencode-7" "$output"
}

@test "a claim with no address prints none, and still describes its holder" {
    env -u CLAUDE_CODE_SESSION_ID -u CLAUDE_CODE_MESSAGING_SOCKET \
        "$CLAIM" take worktree:mainrepo "held" --pid "$OWNER" >/dev/null
    run "$CLAIM" check worktree:mainrepo
    # The positive assertions prove describe() ran at all, so the two absences
    # below are a suppressed line rather than an empty output.
    [ "$status" -eq 1 ]
    contains "held" "$output"
    contains "pid=$OWNER" "$output"
    lacks "message=" "$output"
    lacks "session_id=" "$output"
}

@test "the session field names the process the claim records" {
    "$CLAIM" take worktree:mainrepo "held" --pid "$OWNER" >/dev/null
    run "$CLAIM" check worktree:mainrepo
    contains "owner=$(hostname)-$OWNER pid=$OWNER" "$output"
}
