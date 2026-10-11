#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Where this machine's build and test hosts are configured: the one parser for
# that file. Scripts source it; make runs it (mk/remote.mk), and so do the bats
# suite's setup (for the key list).
#
#   ${XDG_CONFIG_HOME:-$HOME/.config}/helixscreen/build-hosts.env
#
# One file outside the tree, so every worktree and clone shares it.
# HELIX_BUILD_HOSTS_FILE names another. The rules:
#
#   KEY=VALUE      at the start of the line, no spaces around =. KEY must be in
#                  BUILD_HOSTS_KEYS; nothing else is ever set.
#   # comment      only as a whole line. A value holds no whitespace and no
#                  quotes, so "KEY=v # note" and KEY="v" are refused, not half-read.
#   literal        no $ expansion, nothing runs; a trailing \r is dropped.
#   last wins      when a key appears twice.
#   env first      a key already set in the environment keeps its value.
#
# A line that breaks a rule is named on stderr and skipped. docs/devel/
# BUILD_SYSTEM.md "Using your own build/test host" says what each key does.
#
#   . scripts/lib/build_hosts.sh        sets the keys, environment first
#   scripts/lib/build_hosts.sh --keys   the allowlist, one per line
#   scripts/lib/build_hosts.sh --make K K's value for make, environment first, $ doubled

BUILD_HOSTS_KEYS="HELIX_TEST_HOST HELIX_TEST_CONTAINER HELIX_TEST_WORKDIR HELIX_TEST_TREES_HOST
HELIX_TEST_TREES HELIX_TEST_CCACHE HELIX_TEST_LOCK_DIR HELIX_TEST_HOST_AUTO
REMOTE_HOST REMOTE_USER REMOTE_DIR REMOTE_BUILD_DIR"

BUILD_HOSTS_FILE="${HELIX_BUILD_HOSTS_FILE:-${XDG_CONFIG_HOME:-$HOME/.config}/helixscreen/build-hosts.env}"

# Prints "KEY VALUE" for each line the rules accept, in file order.
build_hosts_parse() {
    local line key val n=0 warn
    if [ ! -e "$BUILD_HOSTS_FILE" ]; then return 0; fi
    if [ ! -r "$BUILD_HOSTS_FILE" ]; then
        echo "⚠ cannot read $BUILD_HOSTS_FILE (check its permissions); no build hosts loaded" >&2
        return 0
    fi
    while IFS= read -r line || [ -n "$line" ]; do
        n=$((n + 1))
        line=${line%$'\r'}
        case "$line" in ''|'#'*) continue ;; esac
        warn="$BUILD_HOSTS_FILE:$n:"
        if [[ ! "$line" =~ ^([A-Za-z_][A-Za-z0-9_]*)=(.*)$ ]]; then
            echo "⚠ $warn not KEY=VALUE, skipped" >&2; continue
        fi
        key=${BASH_REMATCH[1]} val=${BASH_REMATCH[2]}
        case " ${BUILD_HOSTS_KEYS//$'\n'/ } " in
            *" $key "*) ;;
            *) echo "⚠ $warn $key is not a build-hosts key, skipped" >&2; continue ;;
        esac
        case "$val" in
            *[[:space:]]*) echo "⚠ $warn $key: whitespace in the value (a trailing comment?), skipped" >&2; continue ;;
            *[\"\']*) echo "⚠ $warn $key: quotes in the value, skipped" >&2; continue ;;
        esac
        printf '%s %s\n' "$key" "$val"
    done < "$BUILD_HOSTS_FILE"
}

# Sets every key the file names that the environment does not, last line winning.
load_build_hosts() {
    local key val
    local -A from_file=()
    while read -r key val; do from_file[$key]=$val; done < <(build_hosts_parse)
    for key in "${!from_file[@]}"; do
        [ -n "${!key+x}" ] || export "$key=${from_file[$key]}"
    done
}

# Fails with one line naming the variable and the file when it is unset, so a
# command that needs a host never guesses one.
require_build_host() { # <variable>
    [ -n "${!1:-}" ] && return 0
    echo "✗ $1 is not set: add $1=<host> to $BUILD_HOSTS_FILE (or export it)" >&2
    return 2
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    case "${1:-}" in
        --keys) printf '%s\n' $BUILD_HOSTS_KEYS ;;
        --make)
            # make reads the result as a value, so $ is doubled to stay literal.
            load_build_hosts
            [ -n "${2:-}" ] || exit 2
            v=${!2:-}; printf '%s\n' "${v//\$/\$\$}" ;;
        *) echo "usage: $0 --keys | --make KEY" >&2; exit 2 ;;
    esac
    exit 0
fi

load_build_hosts
