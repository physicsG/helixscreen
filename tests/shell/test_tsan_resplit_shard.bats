#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Meta-tests for scripts/tsan_resplit_shard.sh, the retry `make test-tsan` runs
# on a shard that died on TSan's reporter abort (#1293).
#
# The fake binary stands in for helix-tests-tsan: it lists a fixed set of test
# names, reads them back through --input-file the way Catch2 does (one escaped
# spec per line), and decides each run's outcome from the names it was given.
# POISON_A and POISON_B abort only when they share a process, which is how the
# real abort behaves: it needs an earlier case's event loop to tear down under
# a later one.

load helpers

SCRIPT="scripts/tsan_resplit_shard.sh"

setup() {
    cd "$BATS_TEST_DIRNAME/../.." || return 1
    WORK="$BATS_TEST_TMPDIR/resplit"
    mkdir -p "$WORK"
    NAMES="$WORK/names.txt"
    LOG="$WORK/shard.log"
    : > "$LOG"
    FAKE="$WORK/fake-tests"
    cat > "$FAKE" <<'EOF'
#!/usr/bin/env bash
names_file=$FAKE_NAMES
input=""
listing=0
while [ $# -gt 0 ]; do
    case "$1" in
        -f) input=$2; shift ;;
        --list-tests) listing=1 ;;
    esac
    shift
done
if [ -n "$input" ]; then
    selected=$(sed -e 's/\\\(.\)/\1/g' "$input")
    [ -n "$FAKE_DROP_ON_INPUT" ] && selected=$(printf '%s\n' "$selected" | grep -v "$FAKE_DROP_ON_INPUT")
else
    selected=$(cat "$names_file")
fi
if [ "$listing" -eq 1 ]; then
    printf '%s\n' "$selected"
    exit 0
fi
has() { printf '%s\n' "$selected" | grep -qx "$1"; }
if has RACY; then
    echo "WARNING: ThreadSanitizer: data race (pid=1)"
    exit 66
fi
if has BROKEN; then
    echo "tests/unit/fake.cpp:1: FAILED:"
    exit 1
fi
if has POISON_SOLO || { has POISON_A && has POISON_B; }; then
    echo "ThreadSanitizer: CHECK failed: sanitizer_common.h:494 \"((i)) < ((size_))\""
    exit 66
fi
echo "All tests passed"
exit 0
EOF
    chmod +x "$FAKE"
    export FAKE_NAMES="$NAMES"
    unset FAKE_DROP_ON_INPUT
}

run_resplit() { # rounds
    run bash "$SCRIPT" "$FAKE" "~[.]" 32 7 "$LOG" "$1"
}

@test "an abort that needs two tests in one process is recovered by splitting them" {
    printf '%s\n' t1 t2 t3 POISON_A POISON_B t4 t5 t6 > "$NAMES"
    run_resplit 3
    [ "$status" -eq 0 ]
    [[ "$output" == *"0 of 8 test(s) still unchecked"* ]]
}

@test "a test that aborts on its own is left unchecked alone, and named" {
    printf '%s\n' t1 t2 t3 t4 t5 t6 t7 POISON_SOLO > "$NAMES"
    run_resplit 3
    [ "$status" -eq 4 ]
    grep -q "1 test(s) UNCHECKED after 3 round(s)" "$LOG"
    [ "$(sed -n '/UNCHECKED after/,$p' "$LOG" | tail -n +2)" = "POISON_SOLO" ]
}

@test "a ThreadSanitizer finding in any piece is reported, never retried away" {
    printf '%s\n' t1 t2 t3 RACY t4 POISON_A POISON_B t5 > "$NAMES"
    run_resplit 3
    [ "$status" -eq 3 ]
    grep -q "WARNING: ThreadSanitizer: data race" "$LOG"
}

@test "a piece failing without a TSan report fails the shard" {
    printf '%s\n' t1 t2 BROKEN t3 > "$NAMES"
    run_resplit 3
    [ "$status" -eq 1 ]
    [[ "$output" == *"exited 1 without a TSan report"* ]]
}

@test "names that do not round-trip through --input-file refuse to split" {
    printf '%s\n' t1 t2 t3 POISON_A POISON_B > "$NAMES"
    export FAKE_DROP_ON_INPUT=t2
    run_resplit 3
    [ "$status" -eq 1 ]
    [[ "$output" == *"do not round-trip"* ]]
}

@test "names carrying Catch2 test-spec syntax reach the binary unescaped" {
    printf '%s\n' 'a, b' '[tagged] name' 'star*' '#hash first' 'back\slash' 'q"uote' > "$NAMES"
    run_resplit 1
    [ "$status" -eq 0 ]
}
