#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Load gate on a BusyBox board for whatever the app is running now.
# usage: DUR=60 HELIX_PERF_PROBE=<wakeup_probe built for the board> embedded_gate.sh <arm> <run#> <label>
# Appends "<arm> run=<n> gate RESULT ..." to $HELIX_PERF_SCRATCH/results/<arm>.txt.
set -uo pipefail
# shellcheck source-path=SCRIPTDIR source=perf_env.sh
. "$(dirname "$0")/perf_env.sh"

ARM=$1
RUN=$2
LABEL=$3
DUR=${DUR:-60}
: "${HELIX_PERF_PROBE:?set HELIX_PERF_PROBE to the wakeup_probe binary built for this board}"
OUT=$HELIX_PERF_SCRATCH/results/$ARM.txt
mkdir -p "$HELIX_PERF_SCRATCH/cyclic"
REMOTE_PROBE=/tmp/wakeup_probe
REMOTE_HIST=/tmp/hm_probe_${ARM}_${LABEL}.txt

perf_ssh "cat > $REMOTE_PROBE && chmod +x $REMOTE_PROBE" < "$HELIX_PERF_PROBE"
raw=$({
    printf 'PROBE=%s\nHIST=%s\nDUR=%s\n' "$REMOTE_PROBE" "$REMOTE_HIST" "$DUR"
    cat "$PERF_HERE/embedded_loadgate.sh"
} | perf_ssh "sh -s" | grep '^GATE_RAW' | tail -n 1)
local_hist=$HELIX_PERF_SCRATCH/cyclic/$ARM-r$RUN-$LABEL.txt
perf_ssh "cat $REMOTE_HIST; rm -f $REMOTE_HIST" > "$local_hist"
python3 "$PERF_HERE/ctparse.py" "$local_hist" "$ARM" "$LABEL" "${raw:-GATE_RAW missing}" |
    sed "s/^/$ARM run=$RUN gate /" >> "$OUT"
tail -n 1 "$OUT"
