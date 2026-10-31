#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Deploys helix-screen from a directory to the Pi and proves which build runs.
# usage: pi3b_deploy.sh <bin-dir> [<tree whose ui_xml and assets are synced>]
# Prints DEVICE_VERSION, DEVICE_RUNG, DEVICE_SHA, DEPLOY_OK or DEPLOY_FAIL, and LOCAL_SHA.
set -euo pipefail
# shellcheck source-path=SCRIPTDIR source=perf_env.sh
. "$(dirname "$0")/perf_env.sh"

BIN_DIR=$1
TREE=${2:-}
[ -f "$BIN_DIR/helix-screen" ] || {
    echo "DEPLOY_FAIL missing $BIN_DIR/helix-screen"
    exit 1
}

perf_run_remote "$PERF_HERE/pi3b_service.sh" ACTION=stop
perf_scp "$BIN_DIR/helix-screen" "$PERF_TARGET:$HELIX_PERF_INSTALL/bin/"
if [ -n "$TREE" ]; then
    for sub in ui_xml assets; do
        perf_rsync_dir "$TREE/$sub" "$HELIX_PERF_INSTALL/$sub"
    done
fi
perf_run_remote "$PERF_HERE/pi3b_service.sh" ACTION=start
echo "LOCAL_SHA $(sha256sum "$BIN_DIR/helix-screen" | cut -c1-16)"
