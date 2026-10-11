#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The shellcheck and clang-format stages of the quality gate (scripts/qc/) fan their
# file lists out through xargs. `xargs -a FILE` is a GNU extension; BSD xargs
# rejects it with "invalid option -- a", lints nothing, and both stages read
# an empty result as a clean one (prestonbrown/helixscreen#1488). These cases
# pin the portable spelling and the corpus count that makes an empty run a
# failure instead of a pass.

load helpers

setup() {
  cd "$BATS_TEST_DIRNAME/../.." || return 1
  export REAL_XARGS
  REAL_XARGS="$(command -v xargs)"
  # A shellcheck that finds nothing: the cases below are about whether the
  # files reach it at all, not what it says about them.
  mock_command_script "shellcheck" 'exit 0'
}

# Runs the real qc_shellcheck body in a full sweep with the timing helper
# stubbed and the scratch dir pointed at the test tmpdir.
run_shellcheck_gate() {
  bash -c '
    STAGED_ONLY=false
    QC_TMP="$1"
    QC_COUNTS="$1/counts"
    . scripts/qc/_lib.sh
    . scripts/qc/shellcheck.sh
    section_time() { :; }
    qc_shellcheck
  ' _ "$BATS_TEST_TMPDIR/qc"
}

@test "no gate reads its file list with xargs -a" {
  run bash -c "cat scripts/quality-checks.sh scripts/qc/*.sh | grep -n 'xargs -a'"
  [ "$status" -eq 1 ]
}

@test "the shellcheck gate lints every script it lists and says how many" {
  run run_shellcheck_gate
  [ "$status" -eq 0 ]
  [[ "$output" =~ \(([0-9]+)\ file\(s\)\ linted\) ]] || fail "no linted count in: $output"
  [ "${BASH_REMATCH[1]}" -gt 10 ] || fail "only ${BASH_REMATCH[1]} file(s) linted"
}

@test "the shellcheck gate survives an xargs that rejects -a" {
  # BSD behaviour: -a is an error, everything else is xargs.
  mock_command_script "xargs" 'if [ "$1" = "-a" ]; then echo "xargs: invalid option -- a" >&2; exit 1; fi; exec "$REAL_XARGS" "$@"'
  run run_shellcheck_gate
  [ "$status" -eq 0 ]
  lacks "invalid option" "$output"
  contains "file(s) linted" "$output"
}

@test "a fan-out that examines nothing fails the shellcheck gate" {
  # An xargs that exits clean without running anything is what a dead fan-out
  # looks like from the outside: no findings, no files. It must not touch
  # stdin: with the list fed by redirect that is harmless, but a gate that
  # still passed the list as an argument would leave the stub waiting on the
  # terminal instead of failing.
  mock_command_script "xargs" 'exit 0'
  run run_shellcheck_gate
  [ "$status" -eq 1 ]
  contains "examined 0 of" "$output"
  lacks "All shell scripts pass" "$output"
}

@test "the clang-format probe counts the files it covered" {
  # The probe lives inside qc_phase2 beside unrelated checks, so it is pinned
  # statically: the worker records every file it saw, and the verdict compares
  # that count against the candidate list.
  run sed -n '/^qc_phase2() {/,/^}/p' scripts/qc/phase2.sh
  [ "$status" -eq 0 ]
  contains '< "$CF_CAND" xargs' "$output"
  contains 'CF_EXAMINED=$(grep -c . "$CF_SEEN")' "$output"
  contains '"$CF_EXAMINED" -ne "$CF_TOTAL"' "$output"
}
