#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# keep-unchanged-mtimes.sh DIR -- CMD [ARGS...]
#
# Runs CMD, then gives every file under DIR whose bytes CMD left unchanged its
# previous mtime back. A generator that rewrites its outputs unconditionally
# (libhv's `make libhv` copies include/hv/* every run) then only moves the
# mtime of a file whose content actually changed, so make does not rebuild
# every object that depends on a header nobody edited.
#
# Exits with CMD's status.
set -uo pipefail

if [[ $# -lt 3 || "$2" != "--" ]]; then
    echo "usage: $0 DIR -- CMD [ARGS...]" >&2
    exit 2
fi
dir="$1"
shift 2

snap=""
if [[ -d "$dir" ]]; then
    snap="$(mktemp -d)"
    trap 'rm -rf "$snap"' EXIT
    cp -a "$dir/." "$snap/"
fi

"$@"
rc=$?

if [[ -n "$snap" ]]; then
    while IFS= read -r -d '' old; do
        rel="${old#"$snap"/}"
        new="$dir/$rel"
        if [[ -f "$new" ]] && cmp -s "$old" "$new"; then
            touch -r "$old" "$new"
        fi
    done < <(find "$snap" -type f -print0)
fi

exit "$rc"
