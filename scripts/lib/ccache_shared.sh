#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs scripts/ccache-shared-config.sh inside the test host's container, for
# test-host-setup.sh and test-host-run.sh. Sourced; defines no state but
# HELIX_TEST_CCACHE_SIZE.

# The test host's cache holds every mirror, mutate checkout and the --commit
# checkout at once.
HELIX_TEST_CCACHE_SIZE="${HELIX_TEST_CCACHE_SIZE:-200G}"

_CCACHE_SHARED_SCRIPT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/ccache-shared-config.sh"

# Prints the host-side command that runs `ccache-shared-config.sh ARGS...` in
# CONTAINER against the cache at CCACHE_DIR. The script travels in the command
# itself (base64), so it is this tree's copy whatever the container has checked
# out.
ccache_shared_cmd() { # <container> <ccache dir> <args...>
    local ctr="$1" dir="$2" b64
    shift 2
    b64=$(base64 < "$_CCACHE_SHARED_SCRIPT" | tr -d '\n') || return 1
    printf 'sudo -n docker exec -e CCACHE_DIR=%q %q bash -lc %q' \
        "$dir" "$ctr" "echo $b64 | base64 -d | bash -s -- $(printf '%q ' "$@")"
}
