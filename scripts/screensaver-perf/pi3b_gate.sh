#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Load-gate pass on whatever is deployed to the Pi.
# usage: SAVERS="off ui toasters" DUR=60 pi3b_gate.sh <arm> <run#>
# off is the idle home panel, ui cycles the base panels, a saver name runs that saver.
# Appends "<arm> run=<n> gate RESULT ..." and "<arm> run=<n> gate <label> THERMAL mid-run ..."
# lines to $HELIX_PERF_SCRATCH/results/<arm>.txt.
set -uo pipefail
# shellcheck source-path=SCRIPTDIR source=perf_env.sh
. "$(dirname "$0")/perf_env.sh"

ARM=$1
RUN=$2
SAVERS=${SAVERS:-"off toasters"}
DUR=${DUR:-60}
OUT=$HELIX_PERF_SCRATCH/results/$ARM.txt
mkdir -p "$HELIX_PERF_SCRATCH/cyclic"

for label in $SAVERS; do
    type=$(perf_saver_type "$label") || exit 2
    ui_loop=0
    [ "$label" = "ui" ] && ui_loop=1
    mid_thermal=$HELIX_PERF_SCRATCH/cyclic/$ARM-r$RUN-$label.thermal
    rm -f "$mid_thermal"
    # Sampled while the load runs: the load, not the pause after it, is what throttles the board.
    (sleep $((DUR / 2 + 5)); perf_thermal > "$mid_thermal") < /dev/null &
    sampler=$!
    raw=$(perf_run_remote "$PERF_HERE/pi3b_loadgate.sh" "ARM=$ARM" "LABEL=$label" "TYPE=$type" \
        "UI_LOOP=$ui_loop" "DUR=$DUR" 2>&1 | grep '^GATE_RAW' | tail -n 1)
    wait "$sampler"
    remote_hist=$(sed -n 's/.*hist=\([^ ]*\).*/\1/p' <<<"$raw")
    local_hist=$HELIX_PERF_SCRATCH/cyclic/$ARM-r$RUN-$label.txt
    if [ -n "$remote_hist" ]; then
        perf_run_remote "$PERF_HERE/pi3b_service.sh" ACTION=collect "FILE=$remote_hist" > "$local_hist"
    else
        : > "$local_hist"
    fi
    python3 "$PERF_HERE/ctparse.py" "$local_hist" "$ARM" "$label" "${raw:-GATE_RAW missing}" |
        sed "s/^/$ARM run=$RUN gate /" >> "$OUT"
    echo "$ARM run=$RUN gate $label THERMAL mid-run $(cat "$mid_thermal" 2>/dev/null)" >> "$OUT"
done
grep " run=$RUN gate " "$OUT" | tail -n 16
