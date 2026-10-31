#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Re-run a ThreadSanitizer shard that died on TSan's own reporter abort
# (prestonbrown/helixscreen#1293), split into ever smaller pieces, so the abort
# costs the coverage of the piece it lands in rather than of the whole shard.
#
#   scripts/tsan_resplit_shard.sh BIN FILTER SHARD_COUNT SHARD_INDEX LOG ROUNDS
#
# The abort is not attributable to one test: it lands on whichever case is
# running when an earlier case's libhv event loop tears down. A shard's
# composition is fixed, so re-running the same shard repeats the same pairing
# and aborts again. Round r instead runs the shard's still-unchecked tests as
# 2^r separate processes, each given its test names through Catch2's
# --input-file; a piece that runs clean is checked, and only the pieces that
# abort again carry into the next round.
#
# TSAN_OPTIONS is taken from the environment. Every piece's output is appended
# to LOG, so a finding in any piece reaches the caller's report scan.
#
# Exit status:
#   0  every test in the shard ran to completion clean in some piece
#   3  a piece reported a ThreadSanitizer finding (never retried away)
#   4  pieces were still aborting after ROUNDS rounds; their tests went
#      unchecked and are listed in LOG
#   1  anything else: a piece failed an assertion or crashed, or the shard's
#      test list could not be read back exactly through --input-file
set -uo pipefail

if [ $# -ne 6 ]; then
    echo "usage: $0 BIN FILTER SHARD_COUNT SHARD_INDEX LOG ROUNDS" >&2
    exit 1
fi
bin=$1 filter=$2 count=$3 index=$4 log=$5 rounds=$6

report_re='(WARNING|ERROR): ThreadSanitizer'
work=$(mktemp -d "${TMPDIR:-/tmp}/tsan-resplit-XXXXXX") || exit 1
trap 'rm -rf "$work"' EXIT

# Catch2 lists a name that starts with '#' wrapped in quotes, reads an input-file
# line that starts with '#' as a comment, and parses each line as a test spec in
# which \ , " [ ] * ~ are syntax. Undo the first, escape the rest.
escape_names() {
    sed -e 's/^"\(.*\)"$/\1/' -e 's/[][\\,"*~]/\\&/g' -e 's/^#/\\#/'
}

list_tests() { # extra args...
    "$bin" "$@" --list-tests --verbosity quiet 2>/dev/null
}

if ! list_tests "$filter" --shard-count "$count" --shard-index "$index" > "$work/pending"; then
    echo "tsan-resplit: cannot list shard $index of $count" | tee -a "$log"
    exit 1
fi
total=$(wc -l < "$work/pending" | tr -d ' ')
if [ "$total" -eq 0 ]; then
    echo "tsan-resplit: shard $index of $count lists no tests" | tee -a "$log"
    exit 1
fi

# The split is only coverage if every name selects exactly itself.
escape_names < "$work/pending" > "$work/all.spec"
if ! list_tests -f "$work/all.spec" > "$work/roundtrip" ||
    ! diff -q <(sort "$work/pending") <(sort "$work/roundtrip") > /dev/null; then
    echo "tsan-resplit: shard $index's test names do not round-trip through --input-file" | tee -a "$log"
    exit 1
fi

for round in $(seq 1 "$rounds"); do
    remaining=$(wc -l < "$work/pending" | tr -d ' ')
    pieces=$((1 << round))
    [ "$pieces" -gt "$remaining" ] && pieces=$remaining
    per=$(((remaining + pieces - 1) / pieces))
    rm -f "$work"/piece.*
    split -l "$per" "$work/pending" "$work/piece."
    : > "$work/next"
    aborted=0
    for piece in "$work"/piece.*; do
        escape_names < "$piece" > "$piece.spec"
        echo "=== tsan-resplit shard $index round $round piece $(basename "$piece") ($(wc -l < "$piece" | tr -d ' ') tests) ===" >> "$log"
        "$bin" -f "$piece.spec" > "$piece.log" 2>&1
        ec=$?
        cat "$piece.log" >> "$log"
        if grep -qE "$report_re" "$piece.log"; then
            echo "tsan-resplit: shard $index round $round: a piece reported a ThreadSanitizer finding"
            exit 3
        fi
        if [ "$ec" -eq 0 ]; then
            continue
        fi
        if grep -q 'CHECK failed' "$piece.log"; then
            cat "$piece" >> "$work/next"
            aborted=$((aborted + 1))
            continue
        fi
        echo "tsan-resplit: shard $index round $round: a piece exited $ec without a TSan report"
        exit 1
    done
    mv "$work/next" "$work/pending"
    left=$(wc -l < "$work/pending" | tr -d ' ')
    echo "tsan-resplit: shard $index round $round: $pieces piece(s), $aborted aborted, $left of $total test(s) still unchecked"
    if [ "$left" -eq 0 ]; then
        exit 0
    fi
done

{
    echo "=== tsan-resplit shard $index: $(wc -l < "$work/pending" | tr -d ' ') test(s) UNCHECKED after $rounds round(s) ==="
    cat "$work/pending"
} >> "$log"
exit 4
