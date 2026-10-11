#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The unit sweep's shard runner (`run_tests_parallel` in mk/tests.mk). With a
# jobpool live it launches shards in batches of three, each batch under one
# `jobpool with-token` held for the batch's whole life, so every sweep on the
# box together runs at most three shards per pool token. Without a pool it
# runs each shard on its own, as it always has.
#
# The test binary and the pool are fakes: the binary prints which token it ran
# under and fails or hangs on request, and the pool logs every token it hands
# out and takes back.

bats_require_minimum_version 1.5.0
load helpers

REPO="$(cd "${BATS_TEST_DIRNAME}/../.." && pwd)"

setup() {
    unset JOBPOOL
    export HELIX_JOBPOOL="$BATS_TEST_TMPDIR/fake-jobpool"
    export POOL_LOG="$BATS_TEST_TMPDIR/pool.log"
    FAKE_BIN="$BATS_TEST_TMPDIR/fake-tests"
    cat > "$FAKE_BIN" <<'EOF'
#!/bin/sh
i=""
while [ $# -gt 0 ]; do
    [ "$1" = --shard-index ] && i=$2
    shift
done
echo "ran under token=${FAKE_TOKEN:-none}"
[ "$i" = "${FAIL_SHARD:-x}" ] && { echo "test cases: 1 | 1 failed"; exit 1; }
[ "$i" = "${HANG_SHARD:-x}" ] && sleep 5
echo "All tests passed"
[ -n "${FAKE_TOKEN:-}" ] && echo "end $FAKE_TOKEN" >> "$POOL_LOG"
exit 0
EOF
    chmod +x "$FAKE_BIN"
}

# A live pool. with-token logs take/give around the command and names the
# token in FAKE_TOKEN so the shards can report which one they ran under.
live_pool() {
    cat > "$HELIX_JOBPOOL" <<'EOF'
#!/bin/sh
case "$1" in
    target) echo 4 ;;
    with-token)
        shift; [ "$1" = -- ] && shift
        n=$(date +%s%N)
        echo "take $n" >> "$POOL_LOG"
        FAKE_TOKEN=$n "$@"; rc=$?
        echo "give $n" >> "$POOL_LOG"
        exit $rc ;;
    *) exit 2 ;;
esac
EOF
    chmod +x "$HELIX_JOBPOOL"
}

# sweep SHARDS [make overrides...]: run the shard runner alone, no build.
sweep() {
    local n=$1; shift
    (cd "$REPO" && make --no-print-directory TEST_BIN="$FAKE_BIN" NPROCS="$n" \
        SHARD_ARTIFACT_ROOT="$BATS_TEST_TMPDIR" SHARD_RETRIES=1 "$@" \
        --eval 'sweep-under-test: ; @$(call run_tests_parallel,"~[.]")' sweep-under-test)
}

token_of() { # <shard> <output>
    printf '%s\n' "$2" | sed -n "s/^\[shard $1\] ran under token=//p"
}

@test "with a pool, shards run three to a token and every token comes back" {
    live_pool
    run sweep 7
    [ "$status" -eq 0 ]
    contains "batches of 3" "$output"
    [ "$(grep -c '^take' "$POOL_LOG")" -eq 3 ]
    [ "$(grep -c '^give' "$POOL_LOG")" -eq 3 ]
    local i
    for i in 0 1 2 3 4 5 6; do [ -n "$(token_of $i "$output")" ]; done
    [ "$(token_of 0 "$output")" = "$(token_of 2 "$output")" ]
    [ "$(token_of 3 "$output")" = "$(token_of 5 "$output")" ]
    [ "$(token_of 2 "$output")" != "$(token_of 3 "$output")" ]
    [ "$(token_of 6 "$output")" != "$(token_of 5 "$output")" ]
    [ "$(token_of 0 "$output")" != none ]
    # A token goes back only after every shard it covers has finished.
    [ "$(grep -c '^end' "$POOL_LOG")" -eq 7 ]
    run awk '$1 == "give" { gone[$2] = 1 } $1 == "end" && gone[$2] { print "late: " $0 }' "$POOL_LOG"
    [ -z "$output" ]
}

@test "a failing and a timed-out shard are reported, and their tokens come back" {
    live_pool
    FAIL_SHARD=1 HANG_SHARD=4 run sweep 6 SHARD_TIMEOUT=1
    [ "$status" -ne 0 ]
    contains "Shard 1 had test failures" "$output"
    contains "Shard 4 failed (exit 124)" "$output"
    contains "One or more test shards failed" "$output"
    [ "$(grep -c '^take' "$POOL_LOG")" -eq 2 ]
    [ "$(grep -c '^give' "$POOL_LOG")" -eq 2 ]
}

@test "a with-token that cannot start still runs its shards" {
    printf '#!/bin/sh\n[ "$1" = target ] && echo 4 && exit 0\nexit 1\n' > "$HELIX_JOBPOOL"
    chmod +x "$HELIX_JOBPOOL"
    run sweep 4
    [ "$status" -eq 0 ]
    local i
    for i in 0 1 2 3; do [ "$(token_of $i "$output")" = none ]; done
}

@test "without a pool every shard runs on its own, untouched" {
    live_pool
    JOBPOOL=0 run sweep 5
    [ "$status" -eq 0 ]
    lacks "batches of" "$output"
    [ ! -e "$POOL_LOG" ]
    local i
    for i in 0 1 2 3 4; do [ "$(token_of $i "$output")" = none ]; done
}
