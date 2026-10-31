#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Where a gate runs the C++ unit sweep: here, or on the configured test host.
# Sourced by scripts/full-test-run.sh and scripts/land-gate.sh, after
# scripts/lib/build_hosts.sh, so both gates answer the question the same way.
#
#   TEST_HOST=1  the test host (refuses when none is configured)
#   TEST_HOST=0  here
#   unset        automatic: with a test host configured, `test-host-run.sh
#                --probe` decides from the link and prints the numbers
#                (HELIX_TEST_HOST_AUTO=0 keeps it local). With no test host
#                configured, here, silently. A test host named as a candidate
#                list is resolved here once (resolve_build_host), and the
#                resolved host is what every later step inherits.
#
#   resolve_sweep_location   sets SWEEP_WHERE to local or remote; returns 2 on
#                            a bad TEST_HOST or a missing test host
#
# Reads TEST_HOST_RUN (the test-host-run.sh to probe with) and
# HELIX_TEST_HOST_AUTO (default 1: a warm test-host sweep finishes before the
# local bats suite does, so the split pays whenever the link does).

# shellcheck disable=SC2034  # SWEEP_WHERE is read by the sourcing script
resolve_sweep_location() {
    case "${TEST_HOST:-}" in
        1) require_build_host HELIX_TEST_HOST || return 2
           SWEEP_WHERE=remote ;;
        0) SWEEP_WHERE=local ;;
        "")
            if [ -z "${HELIX_TEST_HOST:-}" ]; then
                SWEEP_WHERE=local
            elif [ "${HELIX_TEST_HOST_AUTO:-1}" != 1 ]; then
                echo "→ unit sweep runs here (HELIX_TEST_HOST_AUTO=0; TEST_HOST=1 forces the test host)"
                SWEEP_WHERE=local
            elif ! resolve_build_host HELIX_TEST_HOST; then
                echo "→ unit sweep runs here (the test host is unreachable)"
                SWEEP_WHERE=local
            elif "$TEST_HOST_RUN" --probe; then
                SWEEP_WHERE=remote
            else
                SWEEP_WHERE=local
            fi ;;
        *) echo "✗ TEST_HOST must be 1, 0 or unset, not '$TEST_HOST'" >&2; return 2 ;;
    esac
}
