#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# One measurement pass on whatever is deployed to the Pi.
# usage: WORKLOADS="idle toasters starfield pipes" pi3b_pass.sh <arm> <run#>
# Appends "<arm> run=<n> <workload> CPU|FLIPS|STOPPED_MIDRUN|STARTED|THERMAL ..." lines to
# $HELIX_PERF_SCRATCH/results/<arm>.txt and keeps each trace under strace/.
set -uo pipefail
# shellcheck source-path=SCRIPTDIR source=perf_env.sh
. "$(dirname "$0")/perf_env.sh"

ARM=$1
RUN=$2
WORKLOADS=${WORKLOADS:-"idle toasters starfield pipes"}
OUT=$HELIX_PERF_SCRATCH/results/$ARM.txt
mkdir -p "$HELIX_PERF_SCRATCH/strace"

echo "$ARM run=$RUN start $(date '+%F %T') $(perf_ssh "$HELIX_PERF_INSTALL/bin/helix-screen --version 2>&1 | head -n 1")" >> "$OUT"

for workload in $WORKLOADS; do
    type=$(perf_saver_type "$workload") || exit 2
    result=$(perf_run_remote "$PERF_HERE/pi3b_measure.sh" "TYPE=$type" 2>&1)
    echo "$result" | grep -E '^(CPU|STOPPED_MIDRUN|STARTED)' | sed "s/^/$ARM run=$RUN $workload /" >> "$OUT"
    if [ "$type" -ne 0 ]; then
        trace=$HELIX_PERF_SCRATCH/strace/$ARM-r$RUN-$workload.txt
        perf_run_remote "$PERF_HERE/pi3b_service.sh" ACTION=collect FILE=/tmp/hm_strace.txt > "$trace"
        python3 "$PERF_HERE/flips.py" "$trace" | sed "s/^/$ARM run=$RUN $workload /" >> "$OUT"
    fi
    echo "$ARM run=$RUN $workload THERMAL $(perf_thermal)" >> "$OUT"
done

echo "$ARM run=$RUN end $(date '+%F %T')" >> "$OUT"
grep " run=$RUN " "$OUT" | tail -n 40
