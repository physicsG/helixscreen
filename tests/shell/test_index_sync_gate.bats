#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for .githooks/lib/sync-index-to-head.sh, run by post-commit.
#
# `git commit -- <paths>` commits from a temporary index. A pre-commit that
# reformats a path and stages the result there leaves the real index holding
# the pre-format blob while HEAD and the working tree hold the formatted one,
# so `git status` reads MM. The catch half: after such a commit the path reads
# clean. The quiet half, which is the safety contract: an index entry is only
# ever set to HEAD's own blob, so a path whose working tree differs from HEAD,
# a path the commit did not touch, and another path's staged change all stay
# exactly as they were.

load helpers

REPO_ROOT="$(cd "${BATS_TEST_DIRNAME}/../.." && pwd)"

setup() {
    FIX="$BATS_TEST_TMPDIR/sync"
    mkdir -p "$FIX/.githooks/lib"
    cd "$FIX" || return 1
    git init -q .
    git config user.email t@example.com
    git config user.name tester
    cp "$REPO_ROOT/.githooks/post-commit" .githooks/
    cp "$REPO_ROOT/.githooks/lib/sync-index-to-head.sh" .githooks/lib/
    # A formatter that appends a marker to f.txt and stages it, as qc_phase2 does.
    printf '#!/bin/sh\n[ -f f.txt ] && sed -i "s/$/ fmt/" f.txt && git add f.txt\nexit 0\n' \
        > .githooks/pre-commit
    chmod +x .githooks/pre-commit .githooks/post-commit .githooks/lib/sync-index-to-head.sh
    git config core.hooksPath "$FIX/.githooks"
    echo base > f.txt
    echo other > g.txt
    git add f.txt g.txt
    git commit -qm base --no-verify
}

@test "a path the pre-commit hook reformatted reads clean after a pathspec commit" {
    echo edited > f.txt
    git commit -qm edit -- f.txt
    [ "$(cat f.txt)" = "edited fmt" ]
    [ "$(git show HEAD:f.txt)" = "edited fmt" ]
    run git status --porcelain -- f.txt
    [ -z "$output" ] || fail "f.txt still reads '$output'"
    [ "$(git rev-parse :f.txt)" = "$(git rev-parse HEAD:f.txt)" ]
}

@test "without the sync step the same commit leaves MM (the shape it repairs)" {
    rm .githooks/lib/sync-index-to-head.sh
    echo edited > f.txt
    git commit -qm edit -- f.txt
    run git status --porcelain -- f.txt
    [ "$output" = "MM f.txt" ]
}

@test "a committed path edited again on disk keeps its index entry" {
    # The formatter rewrites the file after the commit content is fixed, so the
    # working tree differs from HEAD; nothing about that path may change.
    printf '#!/bin/sh\nsed -i "s/$/ fmt/" f.txt && git add f.txt && echo late >> f.txt\nexit 0\n' \
        > .githooks/pre-commit
    echo edited > f.txt
    git commit -qm edit -- f.txt
    [ "$(git rev-parse :f.txt)" != "$(git rev-parse HEAD:f.txt)" ]
    run git diff HEAD -- f.txt
    contains "+late" "$output"
}

@test "a staged change to a path the commit did not touch is untouched" {
    echo staged > g.txt
    git add g.txt
    staged_blob="$(git rev-parse :g.txt)"
    printf 'other\n' > g.txt
    echo edited > f.txt
    git commit -qm edit -- f.txt
    [ "$(git rev-parse :g.txt)" = "$staged_blob" ]
}

@test "a commit with nothing reformatted leaves the index alone and prints nothing" {
    printf '#!/bin/sh\nexit 0\n' > .githooks/pre-commit
    echo plain > f.txt
    run git commit -qm plain -- f.txt
    [ "$status" -eq 0 ]
    [ -z "$output" ]
    run git status --porcelain -- f.txt
    [ -z "$output" ]
}

@test "a committed path whose name is a glob is judged on that file alone" {
    # a*.txt as a pathspec would also match ab.txt, whose unstaged edit would
    # then read as a working-tree change to the committed path.
    printf '#!/bin/sh\nsed -i "s/$/ fmt/" "a*.txt" && git add -- ":(literal)a*.txt"\nexit 0\n' > .githooks/pre-commit
    echo star > "a*.txt"
    echo ab > ab.txt
    git add "a*.txt" ab.txt
    git commit -qm globs --no-verify
    echo ab-wip > ab.txt
    echo star-edited > "a*.txt"
    git commit -qm edit -- "a*.txt"
    [ "$(git rev-parse ':a*.txt')" = "$(git rev-parse 'HEAD:a*.txt')" ] \
        || fail "a*.txt kept its stale index entry"
}
