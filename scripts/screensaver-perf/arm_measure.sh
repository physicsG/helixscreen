#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# One measured arm on the Pi: optionally build, deploy, smoke-test each saver for crashes,
# then three measurement passes and three load-gate passes, and a summary.
#   ARM=<label>                 names results/<ARM>.txt and <ARM>_summary.txt under $HELIX_PERF_SCRATCH
#   BUILD=1|0                   1 runs `make pi-docker` in $HELIX_PERF_BUILD_TREE; 0 deploys arms/<BIN_FROM>
#   BIN_FROM=<arm>              arm whose binaries BUILD=0 deploys (default ARM)
#   ENV_EXTRA="KEY=VALUE ..."   more drop-in variables; the pacing switches are always set
#   WORKLOADS="..."             measurement workloads (default "idle toasters starfield pipes")
#   SAVERS="..."                load-gate workloads (default "off toasters starfield pipes")
#   BASELINES="<arm> ..."       arms summarized beside this one
# Run detached: setsid nohup scripts/screensaver-perf/arm_measure.sh > <log> 2>&1 < /dev/null &
# It touches the Pi only while it holds device:pi3b and no print is running (PRINT_STATE), runs
# with PERF_PACING_ENV and ENV_EXTRA in a systemd drop-in, checks the app's environment carries
# them (APP_ENV), and on finishing removes the drop-in and restarts the app.
# Writes $HELIX_PERF_SCRATCH/<ARM>.done when it finishes, whether or not it succeeded.
set -uo pipefail
# shellcheck source-path=SCRIPTDIR source=perf_env.sh
. "$(dirname "$0")/perf_env.sh"

ARM=${ARM:?set ARM to a label for this arm}
BUILD=${BUILD:-1}
BIN_FROM=${BIN_FROM:-$ARM}
ENV_EXTRA=${ENV_EXTRA:-}
WORKLOADS=${WORKLOADS:-"idle toasters starfield pipes"}
SAVERS=${SAVERS:-"off toasters starfield pipes"}
BASELINES=${BASELINES:-}
CLAIM=$PERF_REPO/scripts/helix-claim
S=$HELIX_PERF_SCRATCH

DEVICE_TAKEN=0

finish() {
    if [ "$DEVICE_TAKEN" = "1" ]; then
        echo "=== $(date +%T) removing the environment drop-in"
        perf_run_remote "$PERF_HERE/pi3b_service.sh" ACTION=dropin ENV_EXTRA= > /dev/null 2>&1
        perf_run_remote "$PERF_HERE/pi3b_service.sh" ACTION=stop > /dev/null 2>&1
        perf_run_remote "$PERF_HERE/pi3b_service.sh" ACTION=start > /dev/null 2>&1
    fi
    echo "=== $(date +%T) finish rc=${1:-0}"
    touch "$S/$ARM.done"
    exit "${1:-0}"
}
rm -f "$S/$ARM.done"

if [ "$BUILD" = "1" ]; then
    : "${HELIX_PERF_BUILD_TREE:?set HELIX_PERF_BUILD_TREE to the tree to build}"
    cd "$HELIX_PERF_BUILD_TREE" || finish 1
    git log -1 --format='HEAD %h %s'
    unapplied=0
    for patch in "$HELIX_PERF_BUILD_TREE"/patches/lvgl*.patch; do
        if git -C lib/lvgl apply --check "$patch" >/dev/null 2>&1; then
            echo "UNAPPLIED ON HOST: $(basename "$patch")"
            unapplied=1
        fi
    done
    [ "$unapplied" -eq 0 ] || finish 1
    echo "=== $(date +%T) build"
    build_claim="build:$(basename "$HELIX_PERF_BUILD_TREE")"
    "$CLAIM" take "$build_claim" "pi-docker arm $ARM" >/dev/null
    build_jobs=$("$CLAIM" jobs 2>/dev/null)
    make pi-docker NPROC_DOCKER_RUN="$build_jobs" > "$S/pi-$ARM.log" 2>&1
    build_exit=$?
    "$CLAIM" release "$build_claim" >/dev/null
    echo "BUILD_EXIT=$build_exit"
    grep -F "EGL binary carries" "$S/pi-$ARM.log"
    if [ "$build_exit" -ne 0 ]; then
        grep -n -E "error:|✗" "$S/pi-$ARM.log" | head -n 5
        finish 1
    fi
    mkdir -p "$S/arms/$ARM"
    cp build/pi/bin/helix-screen build/pi/bin/helix-screen-egl "$S/arms/$ARM/"
    git rev-parse --short HEAD > "$S/arms/$ARM/.sha"
    BIN_FROM=$ARM
fi

echo "=== $(date +%T) claiming device:pi3b and checking no print is running"
perf_pi3b_take "screensaver measurement arm $ARM" || finish 1
trap perf_pi3b_release EXIT
DEVICE_TAKEN=1

echo "=== $(date +%T) environment drop-in: '$PERF_PACING_ENV $ENV_EXTRA'"
perf_run_remote "$PERF_HERE/pi3b_service.sh" ACTION=dropin "ENV_EXTRA=$PERF_PACING_ENV $ENV_EXTRA"

echo "=== $(date +%T) deploy $BIN_FROM"
"$PERF_HERE/pi3b_deploy.sh" "$S/arms/$BIN_FROM" "${HELIX_PERF_BUILD_TREE:-}" > "$S/deploy-$ARM.log" 2>&1
grep -E "DEVICE_|DEPLOY_|LOCAL_" "$S/deploy-$ARM.log"
grep -q DEPLOY_OK "$S/deploy-$ARM.log" || finish 1

echo "=== $(date +%T) app environment"
perf_run_remote "$PERF_HERE/pi3b_service.sh" ACTION=app_env > "$S/env-$ARM.txt"
for kv in $PERF_PACING_ENV $ENV_EXTRA; do
    if ! grep -q -x -F "$kv" "$S/env-$ARM.txt"; then
        echo "APP_ENV missing $kv: the drop-in did not reach the app"
        finish 1
    fi
done
echo "APP_ENV ok: $PERF_PACING_ENV $ENV_EXTRA"

echo "=== $(date +%T) crash smoke"
since=$(date -u '+%Y-%m-%d %H:%M:%S')
for workload in $WORKLOADS; do
    [ "$workload" = "idle" ] && continue
    WORKLOADS="$workload" "$PERF_HERE/pi3b_pass.sh" "smoke-$ARM" 1 > /dev/null 2>&1
done
crashes=$(perf_run_remote "$PERF_HERE/pi3b_service.sh" ACTION=crashes "SINCE=$since")
echo "CRASHES_DURING_SMOKE=$crashes"
[ "$crashes" = "0" ] || finish 1

since=$(date -u '+%Y-%m-%d %H:%M:%S')
for run in 1 2 3; do
    echo "=== $(date +%T) pass $run"
    WORKLOADS="$WORKLOADS" "$PERF_HERE/pi3b_pass.sh" "$ARM" "$run" > /dev/null 2>&1
    echo "=== $(date +%T) gate $run"
    SAVERS="$SAVERS" DUR=60 "$PERF_HERE/pi3b_gate.sh" "$ARM" "$run" > /dev/null 2>&1
done
crashes=$(perf_run_remote "$PERF_HERE/pi3b_service.sh" ACTION=crashes "SINCE=$since")
echo "CRASHES_DURING_MEASUREMENT=$crashes"
files=()
for arm in $BASELINES $ARM; do
    files+=("$S/results/$arm.txt")
done
python3 "$PERF_HERE/summarize.py" "${files[@]}" > "$S/${ARM}_summary.txt" 2>&1
finish 0
