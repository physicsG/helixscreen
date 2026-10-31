#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# A Catch2 assertion between fork() and the child's branch runs in BOTH
# processes, and one inside the child block runs in the child. Catch2's XML
# writer flushes after every element and the child shares the report's file
# description, so under --success (which reports passing assertions) the
# child's copy lands in the parent's report mid-element and the report stops
# parsing. Check the fork result after the child block.

setup() {
    cd "$BATS_TEST_DIRNAME/../.." || return 1
    load helpers.bash
}

# file:line of every Catch2 macro a forked child would evaluate: after a fork()
# and before the `if` that branches on its result, or inside an `if (... == 0)`
# child block. Braces are counted per line, which the test style here satisfies.
fork_assertion_offenders() {
    awk '
        function macro(line) {
            return line ~ /(^|[^A-Za-z_])(REQUIRE|CHECK|INFO|CAPTURE|FAIL|SUCCEED|WARN|UNSCOPED_INFO|SECTION)[A-Z_]*[[:space:]]*\(/
        }
        FNR == 1 { armed = 0; depth = 0 }
        depth > 0 {
            if (macro($0)) print FILENAME ":" FNR ": " $0
            depth += gsub(/\{/, "{") - gsub(/\}/, "}")
            next
        }
        /=[[:space:]]*fork[[:space:]]*\([[:space:]]*\)[[:space:]]*;/ { armed = 1; next }
        armed && /^[[:space:]]*if[[:space:]]*\(/ {
            armed = 0
            if ($0 ~ /==[[:space:]]*0[[:space:]]*\)/) depth = gsub(/\{/, "{") - gsub(/\}/, "}")
            next
        }
        armed && macro($0) { print FILENAME ":" FNR ": " $0 }
    ' "$@"
}

@test "no test asserts in a forked child" {
    local files
    files=$(grep -rlE '=[[:space:]]*fork[[:space:]]*\(' tests/ --include='*.cpp' || true)
    [ -n "$files" ] || fail "found no fork() in tests/; the gate is looking in the wrong place"
    # shellcheck disable=SC2086
    run fork_assertion_offenders $files
    [ "$status" -eq 0 ]
    [ -z "$output" ] || fail "assertions a forked child would also evaluate:
$output"
}

@test "the gate flags assertions a forked child would evaluate" {
    local f="${BATS_TEST_TMPDIR}/fork_shapes.cpp"
    cat > "$f" <<'SHAPES'
TEST_CASE("a") {
    pid_t pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        _exit(0);
    }
}
TEST_CASE("b") {
    const pid_t child = fork();
    CHECK_FALSE(child < 0);

    if (child == 0) {
        if (setsid() < 0) {
            _exit(98);
        }
        INFO("in the child");
        _exit(0);
    }
    REQUIRE(child > 0);
}
SHAPES
    run fork_assertion_offenders "$f"
    [ "$status" -eq 0 ]
    [ "${#lines[@]}" -eq 3 ] || fail "expected 3 findings, got: $output"
    contains 'INFO("in the child")' "$output"
    contains "REQUIRE(pid >= 0)" "$output"
    contains "CHECK_FALSE(child < 0)" "$output"
}

@test "the gate stays quiet on assertions after the child block" {
    local f="${BATS_TEST_TMPDIR}/fork_ok.cpp"
    cat > "$f" <<'SHAPES'
TEST_CASE("a") {
    pid_t pid = fork();
    if (pid == 0) {
        if (setsid() < 0) {
            _exit(98);
        }
        _exit(0);
    }
    REQUIRE(pid > 0);
    int status = 0;
    REQUIRE(waitpid(pid, &status, 0) == pid);
}
SHAPES
    run fork_assertion_offenders "$f"
    [ "$status" -eq 0 ]
    [ -z "$output" ] || fail "unexpected findings: $output"
}
