#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Produce the shared Catch2 report that the test-quality gates read, under a
# watch that says where the run is and stops it when it stops progressing.
#
# The report is written as the suite runs, one element per assertion, so its
# byte count is a live progress signal: a growing file means assertions are
# still completing, and a file that stops growing means the run is sitting
# inside a single test case. Without a watch the two are indistinguishable from
# outside — the run has no console output, because test stdout has to be
# discarded to keep ANSI status bytes out of the report — so a stuck case looks
# exactly like a slow suite and the gates that consume the report never run.
#
# Usage:
#   scripts/run_suite_report.sh <test-binary> <catch2-filter> <report-path>
#
# Environment:
#   SUITE_REPORT_STALL_SECS     stop the run after this long with no report
#                               growth (default 300)
#   SUITE_REPORT_MAX_SECS       stop the run after this much wall clock
#                               regardless of progress (default 2400)
#   SUITE_REPORT_PROGRESS_SECS  interval between progress lines (default 30)
#
# Exit status:
#   0  the suite ran to the end and left a non-empty report
#   1  the run stalled or hit the wall-clock cap; the report is partial
#   2  no usable report was produced
#
# A non-zero exit from the suite itself is NOT an error here. A red suite is
# `make full-test-run`'s finding to report, and the gates still have something to say
# about the report a failing run produced.

set -uo pipefail

STALL_SECS="${SUITE_REPORT_STALL_SECS:-300}"
MAX_SECS="${SUITE_REPORT_MAX_SECS:-2400}"
PROGRESS_SECS="${SUITE_REPORT_PROGRESS_SECS:-30}"

# Nothing is observed between polls, so the poll can never be coarser than the
# progress interval it has to feed.
POLL_SECS=5
[ "$PROGRESS_SECS" -lt "$POLL_SECS" ] && POLL_SECS="$PROGRESS_SECS"

if [ $# -ne 3 ]; then
    echo "usage: $0 <test-binary> <catch2-filter> <report-path>" >&2
    exit 2
fi

TEST_BIN="$1"
FILTER="$2"
REPORT="$3"

if [ ! -x "$TEST_BIN" ]; then
    echo "run_suite_report: $TEST_BIN is not an executable" >&2
    exit 2
fi

ts() { date '+%Y-%m-%dT%H:%M:%S'; }
say() { echo "$(ts) [suite-report] $*"; }

# BSD stat and GNU stat spell the size flag differently.
if stat -f %z . >/dev/null 2>&1; then
    file_size() { stat -f %z "$1" 2>/dev/null || echo 0; }
else
    file_size() { stat -c %s "$1" 2>/dev/null || echo 0; }
fi

mkdir -p "$(dirname "$REPORT")"
rm -f "$REPORT"
RUN_LOG="$REPORT.stdout"

say "starting $TEST_BIN '$FILTER' (stall ${STALL_SECS}s, cap ${MAX_SECS}s)"

# --out is not optional: tests that shell out print ANSI status to stdout, and
# with the report on stdout too the XML comes back complete but unparseable.
"$TEST_BIN" "$FILTER" --reporter xml --success --out "$REPORT" >"$RUN_LOG" 2>&1 &
run_pid=$!

start=$(date +%s)
scanned=0          # bytes of the report already searched for case names
last_size=0
last_growth=$start
next_progress=$((start + PROGRESS_SECS))
current_case="(none started)"
verdict=""

# Names the case the run is inside by scanning only the bytes added since the
# last look, so a single case larger than any fixed window is still attributed.
refresh_current_case() {
    local found
    found=$(tail -c "+$((scanned + 1))" "$REPORT" 2>/dev/null |
        grep -ao '<TestCase name="[^"]*"' | tail -1)
    if [ -n "$found" ]; then
        found="${found#*name=\"}"
        current_case="${found%\"}"
    fi
    scanned="$last_size"
}

while kill -0 "$run_pid" 2>/dev/null; do
    sleep "$POLL_SECS"
    now=$(date +%s)
    size=$(file_size "$REPORT")

    if [ "$size" -gt "$last_size" ]; then
        last_size="$size"
        last_growth="$now"
    fi

    if [ "$now" -ge "$next_progress" ]; then
        refresh_current_case
        say "$((now - start))s  $((last_size / 1048576)) MB  in: $current_case"
        next_progress=$((now + PROGRESS_SECS))
    fi

    if [ $((now - last_growth)) -ge "$STALL_SECS" ]; then
        verdict="stall"
        break
    fi
    if [ $((now - start)) -ge "$MAX_SECS" ]; then
        verdict="cap"
        break
    fi
done

if [ -n "$verdict" ]; then
    refresh_current_case
    elapsed=$(( $(date +%s) - start ))
    kill -9 "$run_pid" 2>/dev/null
    wait "$run_pid" 2>/dev/null
    if [ "$verdict" = "stall" ]; then
        say "STALLED: the report stopped growing ${STALL_SECS}s ago (elapsed ${elapsed}s)"
    else
        say "TIMED OUT: no result after ${MAX_SECS}s"
    fi
    say "  stalled inside test case: $current_case"
    say "  partial report: $REPORT ($((last_size / 1048576)) MB)"
    say "  suite stdout:   $RUN_LOG"
    say "  re-run that case alone with: $TEST_BIN \"$current_case\""
    exit 1
fi

wait "$run_pid"
suite_rc=$?
elapsed=$(( $(date +%s) - start ))
last_size=$(file_size "$REPORT")

if [ ! -s "$REPORT" ]; then
    say "no report produced (suite exit $suite_rc); see $RUN_LOG"
    exit 2
fi

cases=$(grep -ao '<TestCase name=' "$REPORT" | wc -l | tr -d ' ')
say "done in ${elapsed}s: $((last_size / 1048576)) MB, ${cases:-0} cases, suite exit $suite_rc"
exit 0
