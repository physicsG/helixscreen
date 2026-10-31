#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The one place the shared-cache ccache settings are written, for the cache
# CCACHE_DIR names (or ccache's own default when it is unset).
#
#   ccache-shared-config.sh apply PREFIX [MIN_SIZE]
#       base_dir  covers PREFIX: an existing base_dir that is PREFIX or one of
#                 its ancestors is kept, anything else is replaced by PREFIX.
#       hash_dir  false.
#       max_size  at least MIN_SIZE (default 25G); a larger value or 0
#                 (unlimited) is kept.
#       Idempotent: a cache already configured is left byte-for-byte alone.
#
#   ccache-shared-config.sh check SRC_DIR
#       Warns on stderr and exits 1 when the effective base_dir (environment
#       included) is unset or not a prefix of SRC_DIR, or hash_dir is on.
#
# ccache keys a compile on absolute paths unless base_dir covers the source,
# and hash_dir=true adds the working directory to the key. Every worktree and
# every test-host mirror sits at its own absolute path, so without both
# settings no tree can reuse a compile another tree already paid for.
# A shared cache that misses for this reason still works, only slowly, which
# is why check exists: the failure is otherwise silent.
set -euo pipefail

DEFAULT_MIN_SIZE=25G

usage() { awk 'NR > 2 && /^set -euo/ { exit } NR > 2' "$0" | sed 's/^# \?//'; exit 2; }

# The config file's value, not one an environment variable overrides.
file_config() { # <key>
    env -u CCACHE_BASEDIR -u CCACHE_HASHDIR -u CCACHE_NOHASHDIR -u CCACHE_MAXSIZE \
        ccache --get-config "$1" 2>/dev/null || true
}

# Bytes in a ccache size as --get-config prints it ("150.0 GB", "5.4G",
# "5000.0 GiB") or as a caller writes it ("200G", "25Gi"). Without a unit it is
# a byte count. Prints nothing for a value it cannot read.
size_bytes() { # <size>
    printf '%s\n' "$1" | awk '
        match($0, /^[ \t]*[0-9]+(\.[0-9]+)?/) {
            n = substr($0, RSTART, RLENGTH) + 0
            rest = substr($0, RSTART + RLENGTH); gsub(/[ \t]/, "", rest)
            u = toupper(substr(rest, 1, 1))
            base = (substr(rest, 2, 1) == "i") ? 1024 : 1000
            p = index("KMGT", u)
            if (u != "" && p == 0) exit
            printf "%.0f\n", n * (p ? base ^ p : 1)
        }'
}

# PREFIX is BASE or BASE is one of its ancestors, compared by whole components.
covers() { # <base> <path>
    local base="${1%/}" path="${2%/}"
    [ -n "$base" ] || return 1
    [ "$path" = "$base" ] || [ "${path#"$base"/}" != "$path" ]
}

apply() { # <prefix> [min size]
    local prefix="${1:-}" min="${2:-$DEFAULT_MIN_SIZE}"
    case "$prefix" in /*) ;; *) echo "✗ apply needs an absolute PREFIX" >&2; exit 2 ;; esac
    local min_bytes; min_bytes=$(size_bytes "$min")
    [ -n "$min_bytes" ] || { echo "✗ cannot read size '$min'" >&2; exit 2; }

    local cur; cur=$(file_config base_dir)
    if covers "$cur" "$prefix"; then
        echo "  base_dir: $cur (covers $prefix)"
    else
        ccache --set-config "base_dir=$prefix"
        echo "  base_dir: $prefix (was ${cur:-unset})"
    fi

    cur=$(file_config hash_dir)
    if [ "$cur" = false ]; then
        echo "  hash_dir: false"
    else
        ccache --set-config hash_dir=false
        echo "  hash_dir: false (was ${cur:-unset})"
    fi

    cur=$(file_config max_size)
    local cur_bytes; cur_bytes=$(size_bytes "$cur")
    if [ "$cur_bytes" = 0 ] || { [ -n "$cur_bytes" ] && [ "$cur_bytes" -ge "$min_bytes" ]; }; then
        echo "  max_size: $cur"
    else
        ccache --set-config "max_size=$min"
        echo "  max_size: $min (was ${cur:-unset})"
    fi
}

check() { # <source dir>
    local src="${1:-}"
    [ -n "$src" ] || { echo "✗ check needs SRC_DIR" >&2; exit 2; }
    local base hash ok=0
    base=$(ccache --get-config base_dir 2>/dev/null || true)
    hash=$(ccache --get-config hash_dir 2>/dev/null || true)
    if ! covers "$base" "$src"; then
        echo "⚠ ccache base_dir '${base:-unset}' is not a prefix of $src: this tree cannot reuse other trees' compiles (scripts/ccache-shared-config.sh apply)" >&2
        ok=1
    fi
    if [ "$hash" != false ]; then
        echo "⚠ ccache hash_dir is '${hash:-unset}': the working directory is in every cache key, so no other tree's compile can hit (scripts/ccache-shared-config.sh apply)" >&2
        ok=1
    fi
    return "$ok"
}

case "${1:-}" in
    apply) shift; apply "$@" ;;
    check) shift; check "$@" ;;
    *) usage ;;
esac
