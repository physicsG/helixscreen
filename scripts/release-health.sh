#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compare the newest beta of VERSION.txt's release against stable in telemetry and
# print the table plus a WARN line per metric that trails. Informational: always
# exits 0, with a one-line reason when there is nothing to compare.
#
# usage: scripts/release-health.sh [base-url]
#   RELEASE_HEALTH_FIXTURE   JSON file or text replacing the network (see releaseHealth.ts)
cd "$(dirname "$0")/../server/telemetry-worker" 2>/dev/null || { echo "release-health: server/telemetry-worker missing; skipped"; exit 0; }
npx vite-node scripts/releaseHealth.ts "$@" || echo "release-health: could not run; skipped"
exit 0
