#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# tree_stamp.sh [--paths FILE] [BASE]
#
# Prints "<BASE> <content-hash>": the commit the source tree is measured
# against, and a hash naming every source file under src/, include/ and tests/
# whose working-tree content differs from BASE (tracked edits, deletions and
# untracked sources alike). BASE defaults to HEAD. A tree identical to BASE
# hashes to "clean". --paths limits the measurement to the repository paths
# FILE lists, one per line (scripts/test_link_sources.py writes the set a
# link is built from); FILE is read relative to the caller's directory.
#
# The helix-tests build stamps the binary with this; the binary runs
# `tree_stamp.sh --paths <its list> <its BASE>` at startup and warns when the
# two differ. Measuring against the BASE the binary recorded, not the current
# HEAD, is what lets a commit of already-built edits leave the binary reading
# as current: the same files differ from that BASE with the same content
# either way.
#
# Never writes the index. A porcelain `git diff` refreshes stat-dirty entries
# and rewrites .git/index under index.lock, which in a shared tree fails a
# peer's concurrent commit; refresh is switched off here, and a file whose
# content still equals BASE is dropped by comparing blob hashes instead.
#
# Exit 1 outside a git work tree (an rsynced mirror, an unpacked artifact) or
# when FILE is missing, exit 2 when BASE is not a commit this repository has.
# Both print nothing.

set -u

paths_file=""
if [ "${1:-}" = --paths ]; then
    paths_file=$2
    shift 2
    [ -f "$paths_file" ] || exit 1
    paths_file=$(cd "$(dirname "$paths_file")" && pwd)/$(basename "$paths_file")
fi

top=$(git rev-parse --show-toplevel 2>/dev/null) && [ -n "$top" ] && cd "$top" || exit 1
base=$(git rev-parse --verify --quiet "${1:-HEAD}^{commit}" 2>/dev/null) || exit 2

roots=(src include tests)
git_ro() { git -c diff.autoRefreshIndex=false -c core.fsmonitor=false "$@"; }

# Every path that may differ from BASE, deduplicated and filtered to what the
# compiler reads. Stat-dirty files with unchanged content are listed too; the
# blob comparison below drops them. Renames are a delete plus an add.
candidates=$(
    {
        git_ro diff --no-ext-diff --no-renames --name-only -z "$base" -- "${roots[@]}"
        git_ro ls-files -z -o --exclude-standard -- "${roots[@]}"
    } | while IFS= read -r -d '' f; do
        case $f in
            *.c | *.cc | *.cpp | *.h | *.hh | *.hpp | *.inc | *.m | *.mm) printf '%s\n' "$f" ;;
        esac
    done | LC_ALL=C sort -u
)
if [ -n "$candidates" ] && [ -n "$paths_file" ]; then
    candidates=$(printf '%s\n' "$candidates" | grep -Fxf "$paths_file" || true)
fi

listing=""
if [ -n "$candidates" ]; then
    # "<blob>\t<path>" for every candidate as BASE has it, and as it is now.
    base_blobs=$(printf '%s\n' "$candidates" | tr '\n' '\0' |
        xargs -0 git ls-tree "$base" -- 2>/dev/null | awk -F'\t' '{ split($1, m, " "); print m[3] "\t" $2 }')
    present=$(printf '%s\n' "$candidates" | while IFS= read -r f; do
        if [ -f "$f" ]; then printf '%s\n' "$f"; fi
    done)
    now_blobs=""
    if [ -n "$present" ]; then
        now_blobs=$(paste <(printf '%s\n' "$present" | git hash-object --stdin-paths) \
            <(printf '%s\n' "$present"))
    fi
    listing=$(awk -F'\t' '
        FNR == NR { if ($0 != "") was[$2] = $1; next }
        $0 == "" { next }
        { now[$2] = $1 }
        END {
            for (p in now) if (was[p] != now[p]) print now[p] "\t" p
            for (p in was) if (!(p in now)) print "deleted\t" p
        }' <(printf '%s\n' "$base_blobs") <(printf '%s\n' "$now_blobs") | LC_ALL=C sort -k2)
fi

if [ -z "$listing" ]; then
    printf '%s clean\n' "$base"
else
    printf '%s %s\n' "$base" "$(printf '%s\n' "$listing" | git hash-object --stdin)"
fi
