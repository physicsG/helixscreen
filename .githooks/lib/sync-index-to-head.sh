#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Run by post-commit. `git commit -- <paths>` commits from a temporary index,
# so when pre-commit reformats one of those paths and stages the result there,
# the commit and the working tree get the formatted bytes while the real index
# keeps the pre-format blob: `git status` reads MM with nothing in flight.
#
# For each path this commit touched whose working tree already equals HEAD but
# whose index entry does not, point the index entry at HEAD's blob. The entry
# written is HEAD's own object id and mode, so this can only put committed
# content in the index: never a working-tree byte, never a peer's edit. A path
# whose working tree differs from HEAD is left exactly as it is.
set -u
# Every path below is a file name, never a glob: `a*.txt` must not also match ab.txt.
export GIT_LITERAL_PATHSPECS=1

git rev-parse -q --verify HEAD >/dev/null || exit 0

git diff-tree --no-commit-id --name-only -r -z --root HEAD | \
while IFS= read -r -d '' path; do
    git diff --quiet HEAD -- "$path" 2>/dev/null || continue
    git diff --cached --quiet HEAD -- "$path" 2>/dev/null && continue
    entry="$(git ls-tree HEAD -- "$path")"
    [ -n "$entry" ] || continue
    mode="${entry%% *}"
    sha="${entry#* * }"
    sha="${sha%%$'\t'*}"
    # --cacheinfo alone: `update-index <path>` would hash the working tree into
    # the index, which is the one thing this must never do. The entry carries
    # no stat data, so the next status re-hashes it once and finds it clean.
    git update-index --cacheinfo "$mode,$sha,$path" 2>/dev/null || true
done
exit 0
