#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# `helix-claim run heavy:<what> -- cmd` holds a claim for exactly as long as the
# command runs, owned by the wrapper, and passes the command's status through.
# `resources` is a read-only snapshot that must never hang on an unreachable test host.

load helpers

CLAIM="$(cd "${BATS_TEST_DIRNAME}/../.." && pwd)/scripts/helix-claim"

setup() {
    export HELIX_CLAIM_DIR="$BATS_TEST_TMPDIR/claims"
    mkdir -p "$HELIX_CLAIM_DIR"
    CF="$HELIX_CLAIM_DIR/heavy_t.json"
}

teardown() {
    for f in child gc; do
        [ -s "$BATS_TEST_TMPDIR/$f" ] && kill -KILL "$(cat "$BATS_TEST_TMPDIR/$f")" 2>/dev/null
    done
    for p in ${OWNER:-} ${WRAP:-}; do kill "$p" 2>/dev/null; done
    return 0
}

wait_for_file() {
    for _ in $(seq 50); do [ -s "$1" ] && return 0; sleep 0.1; done
    return 1
}

@test "run releases the claim after a normal exit" {
    run "$CLAIM" run heavy:t -- true
    [ "$status" -eq 0 ]
    [ ! -e "$CF" ]
}

@test "run passes a non-zero exit through and still releases" {
    run "$CLAIM" run heavy:t --note n -- sh -c 'exit 7'
    [ "$status" -eq 7 ]
    [ ! -e "$CF" ]
}

@test "the claim is held while the command runs" {
    run "$CLAIM" run heavy:t -- sh -c "cat '$CF'"
    [ "$status" -eq 0 ]
    contains '"resource": "heavy:t"' "$output"
}

@test "SIGTERM to the wrapper stops the command and releases" {
    "$CLAIM" run heavy:t -- sh -c "echo \$\$ > '$BATS_TEST_TMPDIR/child'; exec sleep 60" &
    WRAP=$!
    wait_for_file "$BATS_TEST_TMPDIR/child"
    [ -e "$CF" ]
    kill -TERM "$WRAP"
    local rc=0; wait "$WRAP" || rc=$?
    [ "$rc" -eq 143 ]
    [ ! -e "$CF" ]
    refute kill -0 "$(cat "$BATS_TEST_TMPDIR/child")" 2>/dev/null
}

@test "SIGTERM to the wrapper kills the command's grandchildren too" {
    "$CLAIM" run heavy:t -- sh -c "sleep 300 & echo \$! > '$BATS_TEST_TMPDIR/gc'; wait" &
    WRAP=$!
    wait_for_file "$BATS_TEST_TMPDIR/gc"
    kill -TERM "$WRAP"
    local rc=0; wait "$WRAP" || rc=$?
    [ "$rc" -eq 143 ]
    sleep 0.2
    refute kill -0 "$(cat "$BATS_TEST_TMPDIR/gc")" 2>/dev/null
    [ ! -e "$CF" ]
}

@test "SIGINT to the wrapper stops the command and releases" {
    # A background job starts with SIGINT ignored; restore it as a terminal would.
    env --default-signal=INT "$CLAIM" run heavy:t -- sh -c "echo \$\$ > '$BATS_TEST_TMPDIR/child'; exec sleep 60" &
    WRAP=$!
    wait_for_file "$BATS_TEST_TMPDIR/child"
    kill -INT "$WRAP"
    local rc=0; wait "$WRAP" || rc=$?
    [ "$rc" -eq 130 ]
    sleep 0.2
    refute kill -0 "$(cat "$BATS_TEST_TMPDIR/child")" 2>/dev/null
    [ ! -e "$CF" ]
}

@test "the claim stays LIVE while the command outlives a SIGKILLed wrapper" {
    "$CLAIM" run heavy:t -- sh -c "echo \$\$ > '$BATS_TEST_TMPDIR/child'; exec sleep 60" &
    WRAP=$!
    wait_for_file "$BATS_TEST_TMPDIR/child"
    kill -KILL "$WRAP"
    wait "$WRAP" || true
    run "$CLAIM" check heavy:t
    [ "$status" -eq 1 ]
    contains "LIVE" "$output"
    kill -KILL "$(cat "$BATS_TEST_TMPDIR/child")"
    sleep 0.2
    run "$CLAIM" check heavy:t
    [ "$status" -eq 0 ]
}

@test "stdin reaches the command" {
    run bash -c "echo hi | '$CLAIM' run heavy:t -- cat 2>/dev/null"
    [ "$status" -eq 0 ]
    [ "$output" = hi ]
}

@test "the claim chatter stays off the command's stdout" {
    run bash -c "'$CLAIM' run heavy:t -- echo out 2>/dev/null"
    [ "$output" = out ]
    sleep 120 &
    OWNER=$!
    "$CLAIM" take heavy:t "someone else" --pid "$OWNER" >/dev/null
    run bash -c "'$CLAIM' run heavy:t -- echo out 2>/dev/null"
    [ -z "$output" ]
}

@test "an option with no value exits instead of spinning" {
    run timeout 2 "$CLAIM" run heavy:t --note
    [ "$status" -ne 0 ] && [ "$status" -ne 124 ]
    run timeout 2 "$CLAIM" take device:x op --pid
    [ "$status" -ne 0 ] && [ "$status" -ne 124 ]
    run timeout 2 "$CLAIM" take device:x op --note
    [ "$status" -ne 0 ] && [ "$status" -ne 124 ]
}

@test "a refused take leaves the command unrun" {
    sleep 120 &
    OWNER=$!
    "$CLAIM" take heavy:t "someone else" --pid "$OWNER" >/dev/null
    run "$CLAIM" run heavy:t -- touch "$BATS_TEST_TMPDIR/ran"
    [ "$status" -eq 75 ]
    contains "REFUSED" "$output"
    [ ! -e "$BATS_TEST_TMPDIR/ran" ]
    contains "someone else" "$(cat "$CF")"
}

@test "list shows the process-tree RSS of a live heavy claim" {
    "$CLAIM" run heavy:t -- sh -c "echo x > '$BATS_TEST_TMPDIR/child'; exec sleep 60" &
    WRAP=$!
    wait_for_file "$BATS_TEST_TMPDIR/child"
    run "$CLAIM" list
    contains "heavy:t" "$output"
    contains "rss=" "$output"
}

@test "list shows no RSS for a claim that is not heavy" {
    sleep 120 &
    OWNER=$!
    "$CLAIM" take device:x "hw" --pid "$OWNER" >/dev/null
    run "$CLAIM" list
    lacks "rss=" "$output"
}

@test "resources with the test host unreachable prints this box and exits 0" {
    sleep 120 &
    OWNER=$!
    "$CLAIM" take device:x "hw" --pid "$OWNER" --note "192.0.2.7" >/dev/null
    HELIX_TEST_HOST=testhost.invalid run timeout 10 "$CLAIM" resources
    [ "$status" -eq 0 ]
    contains "cpus, build -j" "$output"
    contains "device:x" "$output"
    contains "192.0.2.7" "$output"
    contains "memory by command" "$output"
    contains "test host (testhost.invalid): unreachable" "$output"
}

@test "resources --no-test-host never reaches for the test host" {
    mock_command_script ssh 'touch "$BATS_TEST_TMPDIR/ssh-called"; exit 255'
    HELIX_TEST_HOST=testhost.invalid run "$CLAIM" resources --no-test-host
    [ "$status" -eq 0 ]
    lacks "test host" "$output"
    [ ! -e "$BATS_TEST_TMPDIR/ssh-called" ]
}

@test "resources skips the test-host section when none is configured" {
    mock_command_script ssh 'touch "$BATS_TEST_TMPDIR/ssh-called"; exit 255'
    run "$CLAIM" resources
    [ "$status" -eq 0 ]
    contains "memory by command" "$output"
    lacks "test host" "$output"
    [ ! -e "$BATS_TEST_TMPDIR/ssh-called" ]
    # The build-hosts file is enough to turn it on.
    echo "HELIX_TEST_HOST=testhost.invalid" > "$HELIX_BUILD_HOSTS_FILE"
    run "$CLAIM" resources
    contains "test host (testhost.invalid): unreachable" "$output"
}

@test "resources cuts off a test host that hangs and still exits 0" {
    mock_command_script ssh 'exec sleep 120'
    SECONDS=0
    HELIX_TEST_HOST=testhost.invalid run timeout 100 "$CLAIM" resources
    [ "$status" -eq 0 ]
    [ "$SECONDS" -lt 90 ]
    contains "unreachable" "$output"
}
