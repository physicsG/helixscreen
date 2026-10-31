#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/release-health.sh against a fixture instead of the network.

load helpers

setup() {
    REPO_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"
    cd "$REPO_ROOT" || return 1
    # Without the worker's installed dependencies npx fetches the newest vite-node,
    # which need not run on this node. CI runs `npm ci` there first; a worktree
    # links them from the main tree (setup-worktree.sh).
    [ -d server/telemetry-worker/node_modules/vite-node ] ||
        skip "server/telemetry-worker has no installed node_modules (npm ci there)"
    FIXTURE="$BATS_TEST_TMPDIR/fixture.json"
}

row() { # version devices crash_free
    printf '{"version":"%s","active_devices":%s,"crash_free_devices":%s,"crash_rate":0.01,"update_failure_rate":0.01,"update_failures":1,"update_successes":99,"print_success_rate":0.9,"p95_frame_ms":20,"p95_rss_kb":100000}' "$1" "$2" "$3"
}

@test "a candidate trailing stable prints a WARN line and exits 0" {
    ver=$(tr -d '[:space:]' <VERSION.txt)
    core=${ver%%-*}
    printf '{"stable":"0.0.1","seen":["%s-beta.98","%s-beta.99","0.0.1"],"versions":[%s,%s]}' "$core" "$core" "$(row "$core-beta.99" 50 0.90)" "$(row 0.0.1 500 0.99)" >"$FIXTURE"
    RELEASE_HEALTH_FIXTURE="$FIXTURE" run scripts/release-health.sh
    [ "$status" -eq 0 ] || fail "$output"
    [[ "$output" == *"WARN crash-free devices"* ]] || fail "$output"
    [[ "$output" == *"$core-beta.99"* ]] || fail "$output"
    [[ "$output" == *"warns on"* ]] || fail "$output"
}

@test "a small candidate sample is noted" {
    ver=$(tr -d '[:space:]' <VERSION.txt)
    core=${ver%%-*}
    printf '{"stable":"0.0.1","versions":[%s,%s]}' "$(row "$core-beta.99" 3 0.99)" "$(row 0.0.1 500 0.99)" >"$FIXTURE"
    RELEASE_HEALTH_FIXTURE="$FIXTURE" run scripts/release-health.sh
    [ "$status" -eq 0 ] || fail "$output"
    [[ "$output" == *"only 3 devices"* ]] || fail "$output"
    [[ "$output" != *"WARN"* ]] || fail "$output"
}

@test "no candidate in telemetry gives a reason and exits 0" {
    printf '{"stable":"0.0.1","versions":[%s]}' "$(row 0.0.1 500 0.99)" >"$FIXTURE"
    RELEASE_HEALTH_FIXTURE="$FIXTURE" run scripts/release-health.sh
    [ "$status" -eq 0 ] || fail "$output"
    [[ "$output" == *"nothing to compare"* ]] || fail "$output"
}

@test "unusable fixture text still exits 0" {
    RELEASE_HEALTH_FIXTURE="not json" run scripts/release-health.sh
    [ "$status" -eq 0 ] || fail "$output"
    [[ "$output" == *"release-health:"* ]] || fail "$output"
}
