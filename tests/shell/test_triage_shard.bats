#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/triage_shard.sh, the per-shard post-mortem the unit sweep runs on a
# shard that failed. It names the failing case from the shard's console log,
# re-runs it alone, alone on one pinned CPU, as the whole shard, and as the
# whole shard pinned, and prints one verdict.
#
# The fake binary lists three cases for the shard (one with a comma, which only
# reaches --input-file intact escaped) and decides each run from FAKE_MODE:
#   alone        VICTIM fails whenever it runs
#   composition  VICTIM fails only after POLLUTER ran in the same process
#   pinned       VICTIM fails only under the fake taskset
#   composition-pinned  both conditions at once
#   hang         every re-run outlives the timeout
#   never        every re-run passes
# The fake taskset reports an affinity list and records the core it pinned to.

load helpers

SCRIPT="scripts/triage_shard.sh"

setup() {
    cd "$BATS_TEST_DIRNAME/../.." || return 1
    DIR="$BATS_TEST_TMPDIR/shards"
    mkdir -p "$DIR"
    FAKE="$BATS_TEST_TMPDIR/fake-tests"
    CALLS="$BATS_TEST_TMPDIR/calls.log"
    export CALLS
    cat > "$FAKE" <<'EOF'
#!/usr/bin/env bash
echo "$* pinned=${FAKE_PINNED:-0}" >> "$CALLS"
input="" listing=0 shard=""
while [ $# -gt 0 ]; do
    case "$1" in
        --input-file) input=$2; shift ;;
        --list-tests) listing=1 ;;
        --shard-index) shard=$2; shift ;;
    esac
    shift
done
if [ "$listing" -eq 1 ]; then
    cat <<'XML'
<?xml version="1.0" encoding="UTF-8"?>
<MatchingTests>
  <TestCase><Name>polluter</Name><SourceInfo><File>tests/unit/test_a.cpp</File><Line>10</Line></SourceInfo></TestCase>
  <TestCase><Name>bystander, with a comma</Name><SourceInfo><File>tests/unit/test_a.cpp</File><Line>20</Line></SourceInfo></TestCase>
  <TestCase><Name>victim</Name><SourceInfo><File>tests/unit/test_b.cpp</File><Line>30</Line></SourceInfo></TestCase>
</MatchingTests>
XML
    exit 0
fi
if [ -n "$input" ]; then
    selected=$(sed -e 's/\\\(.\)/\1/g' "$input")
    if ! printf '%s\n' "$selected" | grep -qx victim; then
        echo "No test cases matched '$(head -1 "$input")'"
        exit 2
    fi
else
    selected=$(printf '%s\n' polluter "bystander, with a comma" victim)
fi
has() { printf '%s\n' "$selected" | grep -qx "$1"; }
fail=0
case ${FAKE_MODE:-never} in
    alone) fail=1 ;;
    composition) has polluter && fail=1 ;;
    pinned) [ "${FAKE_PINNED:-0}" = 1 ] && fail=1 ;;
    composition-pinned) has polluter && [ "${FAKE_PINNED:-0}" = 1 ] && fail=1 ;;
    hang) sleep 30 ;;
esac
if [ "$fail" = 1 ]; then
    echo "tests/unit/test_b.cpp:33: FAILED:"
    echo "test cases: 1 | 1 failed"
    exit 1
fi
echo "All tests passed"
exit 0
EOF
    chmod +x "$FAKE"
    TASKSET="$BATS_TEST_TMPDIR/fake-taskset"
    PINS="$BATS_TEST_TMPDIR/pins.log"
    export PINS
    cat > "$TASKSET" <<'EOF'
#!/usr/bin/env bash
if [ "$1" = -cp ]; then
    echo "pid $2's current affinity list: 4-7,9"
    exit 0
fi
[ "$1" = -c ] || exit 64
echo "$2" >> "$PINS"
shift 2
FAKE_PINNED=1 exec "$@"
EOF
    chmod +x "$TASKSET"
    export TRIAGE_TASKSET="$TASKSET"
    unset TRIAGE_CPU TRIAGE_RETRIES TRIAGE_BUDGET TRIAGE_TIMEOUT TRIAGE_DEADLINE FAKE_MODE
    # Two /proc/stat samples in which CPU 4 idled most of the allowed 4-7,9.
    write_stat before 100 100 100 100 100 100
    write_stat after 100 900 180 170 160 150
    export TRIAGE_STAT_BEFORE="$BATS_TEST_TMPDIR/stat.before"
    export TRIAGE_STAT_AFTER="$BATS_TEST_TMPDIR/stat.after"
    write_failing_log
}

# The shard's console log: the victim's header naming a section, the
# section's location (not the case's), and a FAILED line.
write_failing_log() {
    local rule
    rule=$(printf -- '-%.0s' $(seq 79))
    cat > "$DIR/5.log" <<EOF
=== shard 5/12 host=fake nproc=32 git=abc1234 ts=now order=decl seed=0
[spdlog] noise that is not a header
$rule
victim
  a section
$rule
tests/unit/test_b.cpp:31
...............................................................................

tests/unit/test_b.cpp:33: FAILED:
  REQUIRE( icon_h > 0 )
test cases: 3 | 2 passed | 1 failed
EOF
}

# write_stat NAME IDLE2 IDLE4 IDLE5 IDLE6 IDLE7 IDLE9: a /proc/stat with those
# idle counts (CPU 2 lies outside the fake affinity list).
write_stat() {
    local name=$1
    shift
    {
        echo "cpu  1 2 3 4 5 6 7"
        local c
        for c in 2 4 5 6 7 9; do
            echo "cpu$c 10 0 10 $1 0 0 0 0 0 0"
            shift
        done
    } > "$BATS_TEST_TMPDIR/stat.$name"
}

triage() {
    run bash "$SCRIPT" "$FAKE" "$DIR" 5 12 "~[.] ~[slow]" "$@"
}

@test "a case that fails on its own is a real bug, and is the only stage that runs" {
    FAKE_MODE=alone triage
    [ "$status" -eq 0 ]
    contains "verdict: fails alone" "$output"
    contains "alone: failed 3/3" "$output"
    lacks "composition:" "$output"
    [ "$(cat "$DIR/5.failed")" = victim ]
    contains "repro: $FAKE --input-file $DIR/5.failed" "$output"
    [ ! -e "$PINS" ]
}

@test "a case that fails only on one core is timing, pinned to a core this process may use" {
    FAKE_MODE=pinned triage
    contains "alone: failed 0/3" "$output"
    contains "alone-pinned: failed 3/3" "$output"
    contains "verdict: fails only pinned" "$output"
    contains "pinned to CPU 4" "$output"
    contains "repro: $TASKSET -c 4 $FAKE --input-file" "$output"
    [ "$(sort -u "$PINS")" = 4 ]
}

@test "the pinned core is the allowed one that sat idle longest" {
    write_stat after 9000 300 200 200 800 100
    FAKE_MODE=pinned triage
    contains "repro: $TASKSET -c 7 " "$output"
    contains "pinned to CPU 7" "$output"
}

@test "a run that outlives the timeout is reported as timed out, never as a reproduction" {
    FAKE_MODE=hang TRIAGE_TIMEOUT=1 TRIAGE_BUDGET=30 TRIAGE_RETRIES=1 triage
    [ "$status" -eq 0 ]
    lacks "verdict: fails" "$output"
    contains "verdict: did not reproduce" "$output"
    contains "alone: failed 0/1, 1 timed out at 1s" "$output"
    contains "timed out: 4 run(s) at 1s" "$output"
}

@test "a passed sweep-wide deadline skips the re-runs" {
    FAKE_MODE=alone TRIAGE_DEADLINE=$(date +%s) triage
    [ "$status" -eq 0 ]
    contains "skipped: alone (budget)" "$output"
    contains "failing case(s)" "$output"
    lacks "alone: failed" "$output"
}

@test "a case that fails only after an earlier case is order dependence, with the shard repro" {
    FAKE_MODE=composition triage --order rand --rng-seed 29499066
    contains "alone: failed 0/3" "$output"
    contains "alone-pinned: failed 0/3" "$output"
    contains "composition: failed 3/3" "$output"
    contains "verdict: fails only in shard composition" "$output"
    contains "repro: $FAKE '~[.] ~[slow]' --order rand --rng-seed 29499066 --shard-count 12 --shard-index 5" "$output"
    # The whole-shard re-runs carry the sweep's own order flags.
    grep -q -- "--order rand --rng-seed 29499066 --shard-count 12 --shard-index 5 pinned=0" "$CALLS"
}

@test "a shard that needs both its composition and one core is reported as timing" {
    FAKE_MODE=composition-pinned triage
    contains "composition: failed 0/3" "$output"
    contains "composition-pinned: failed 3/3" "$output"
    contains "verdict: fails only pinned" "$output"
    contains "repro: $TASKSET -c 4 $FAKE" "$output"
}

@test "nothing reproducing says so and does not claim the diff is clean" {
    FAKE_MODE=never TRIAGE_RETRIES=2 triage
    contains "alone: failed 0/2" "$output"
    contains "composition-pinned: failed 0/2" "$output"
    contains "verdict: did not reproduce" "$output"
    contains "does not prove the diff" "$output"
}

@test "TRIAGE_CPU picks the pinned core" {
    FAKE_MODE=pinned TRIAGE_CPU=2 triage
    contains "repro: $TASKSET -c 2 " "$output"
}

@test "without taskset the pinned stages are skipped and named" {
    FAKE_MODE=pinned TRIAGE_TASKSET="$BATS_TEST_TMPDIR/no-such-taskset" triage
    [ "$status" -eq 0 ]
    lacks "pinned: failed" "$output"
    contains "verdict: did not reproduce" "$output"
    contains "pinned stages (" "$output"
}

@test "a shard that died with no FAILED case skips the alone stages" {
    printf '=== shard 5/12\nSegmentation fault\n' > "$DIR/5.log"
    FAKE_MODE=composition triage
    contains "no FAILED marker" "$output"
    lacks "alone: failed" "$output"
    contains "verdict: fails only in shard composition" "$output"
    [ ! -s "$DIR/5.failed" ]
}

@test "a shard killed with no FAILED case is placed by its progress record" {
    printf '=== shard 5/12\n' > "$DIR/5.log"

    printf 'running victim\n' > "$DIR/5.progress"
    FAKE_MODE=never triage
    contains "no FAILED marker; died inside: victim" "$output"
    lacks "teardown" "$output"

    printf 'after victim\n' > "$DIR/5.progress"
    FAKE_MODE=never triage
    contains "died after victim returned" "$output"

    printf 'run-ended\nstale tail\n' > "$DIR/5.progress"
    FAKE_MODE=never triage
    contains "every case had finished: died in exit-time teardown" "$output"
}

@test "a shard killed with no FAILED case and no progress record does not guess" {
    printf '=== shard 5/12\n' > "$DIR/5.log"
    FAKE_MODE=never triage
    contains "no progress record" "$output"
    lacks "teardown" "$output"
}

@test "a SIGTERM at a case's line is the shard timeout, not a failing assertion" {
    local rule
    rule=$(printf -- '-%.0s' $(seq 79))
    cat > "$DIR/5.log" <<EOF
=== shard 5/12 host=fake nproc=32 git=abc1234 ts=now order=decl seed=0
$rule
victim
$rule
tests/unit/test_b.cpp:30
...............................................................................

tests/unit/test_b.cpp:30: FAILED:
due to a fatal error condition:
  SIGTERM - Termination request signal

EOF
    FAKE_MODE=never triage
    [ "$status" -eq 0 ]
    contains "killed by the shard timeout (SIGTERM) while running: tests/unit/test_b.cpp:30" "$output"
    lacks "failing assertion(s)" "$output"
    lacks "no FAILED marker" "$output"
}

@test "an assertion before a SIGTERM is still reported as failing" {
    cat >> "$DIR/5.log" <<'EOF'
tests/unit/test_c.cpp:12: FAILED:
due to a fatal error condition:
  SIGTERM - Termination request signal
EOF
    FAKE_MODE=never triage
    contains "failing assertion(s): tests/unit/test_b.cpp:33" "$output"
    contains "while running: tests/unit/test_c.cpp:12" "$output"
}

@test "the budget bounds the triage and names the stages it skipped" {
    FAKE_MODE=never TRIAGE_BUDGET=0 triage
    [ "$status" -eq 0 ]
    contains "skipped: alone (budget)" "$output"
    lacks "failed 0/" "$output"
}

@test "the sweep's diagnostics run the triage on a failing shard" {
    FAKE_MODE=alone run scratch_make TEST_BIN="$FAKE" SHARD_RETRIES=1 \
        --eval 'triage-under-test: ; @$(call diagnose_shards,'"$DIR"',5,"~[.]",12,)' \
        triage-under-test
    [ "$status" -eq 0 ]
    contains "shard diagnostics" "$output"
    contains "alone: failed 1/1" "$output"
    contains "verdict: fails alone" "$output"
}
