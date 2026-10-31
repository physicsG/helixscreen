#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/tree_stamp.sh, the source-tree identity the helix-tests link rule
# compiles into the binary and the binary re-measures at startup to warn that
# it is stale (tests/test_helpers/stale_binary_check.h). Each case builds a
# throwaway repository, so nothing here reads or writes this checkout's git.

load helpers

REPO_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"
STAMP="$REPO_ROOT/scripts/tree_stamp.sh"

setup() {
    # A hook-run suite inherits GIT_DIR and friends; they would aim every git
    # call below at the real repository.
    unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR
    export GIT_CONFIG_NOSYSTEM=1 HOME="$BATS_TEST_TMPDIR/home"
    mkdir -p "$HOME"
    git config --global user.email t@example.invalid
    git config --global user.name Test
    git config --global init.defaultBranch main
    R="$BATS_TEST_TMPDIR/repo"
    mkdir -p "$R/src" "$R/include" "$R/tests" "$R/docs"
    echo 'int a;' > "$R/src/a.cpp"
    echo '#pragma once' > "$R/include/a.h"
    echo 'readme' > "$R/docs/README.md"
    printf 'build/\n' > "$R/.gitignore"
    git -C "$R" init -q
    git -C "$R" add -A
    git -C "$R" commit -qm init
}

stamp() { (cd "$R/${2:-}" && "$STAMP" ${1:+"$1"}); }

@test "a tree identical to HEAD is clean against HEAD" {
    run stamp
    [ "$status" -eq 0 ]
    [ "$output" = "$(git -C "$R" rev-parse HEAD) clean" ]
}

@test "editing a source file changes the stamp, and changing it back restores it" {
    clean=$(stamp)
    echo 'int b;' >> "$R/src/a.cpp"
    edited=$(stamp)
    [ "$edited" != "$clean" ]
    echo 'int c;' >> "$R/src/a.cpp"
    [ "$(stamp)" != "$edited" ]
    git -C "$R" show HEAD:src/a.cpp > "$R/src/a.cpp"
    [ "$(stamp)" = "$clean" ]
}

# A file whose stat no longer matches the index but whose content does: what
# clang-format, a mutant revert or an rsync leaves behind.
make_stat_dirty() {
    touch -d '2001-01-01' "$R/src/a.cpp"
    # Age the index past the racy-git window so git trusts its stat data.
    touch -d '2000-01-01' "$R/.git/index"
}

index_state() { stat -c '%Y %s' "$R/.git/index" && sha256sum < "$R/.git/index"; }

@test "touching a file without changing it leaves the stamp alone" {
    clean=$(stamp)
    make_stat_dirty
    [ "$(stamp)" = "$clean" ]
}

@test "a stamp run never writes the index, even with stat-dirty entries" {
    make_stat_dirty
    before=$(index_state)
    stamp > /dev/null
    stamp "$(git -C "$R" rev-parse HEAD)" > /dev/null
    [ "$(index_state)" = "$before" ]
    [ ! -e "$R/.git/index.lock" ]
}

@test "--paths measures only the files listed" {
    printf 'src/a.cpp\n' > "$BATS_TEST_TMPDIR/paths"
    clean=$(stamp)
    echo '#define X 1' >> "$R/include/a.h"
    [ "$(cd "$R" && "$STAMP" --paths "$BATS_TEST_TMPDIR/paths")" = "$clean" ]
    echo 'int b;' >> "$R/src/a.cpp"
    [ "$(cd "$R" && "$STAMP" --paths "$BATS_TEST_TMPDIR/paths")" != "$clean" ]
}

@test "--paths with a missing list exits 1 and prints nothing" {
    run bash -c "cd '$R' && '$STAMP' --paths '$BATS_TEST_TMPDIR/absent'"
    [ "$status" -eq 1 ]
    [ -z "$output" ]
}

@test "committing already-stamped edits keeps the stamp measured against the old base" {
    base=$(git -C "$R" rev-parse HEAD)
    echo 'int b;' >> "$R/src/a.cpp"
    echo 'int n;' > "$R/tests/new.cpp"
    built=$(stamp)
    git -C "$R" add -A
    git -C "$R" commit -qm edit
    [ "$(stamp "$base")" = "$built" ]
    # Against the new HEAD the same tree is clean.
    [ "$(stamp)" = "$(git -C "$R" rev-parse HEAD) clean" ]
}

@test "untracked sources and deletions count; non-source files do not" {
    clean=$(stamp)
    echo 'notes' > "$R/docs/NOTES.md"
    echo 'notes' > "$R/src/notes.txt"
    mkdir -p "$R/build" && echo 'int o;' > "$R/build/obj.cpp"
    [ "$(stamp)" = "$clean" ]
    echo 'int u;' > "$R/src/untracked.cpp"
    untracked=$(stamp)
    [ "$untracked" != "$clean" ]
    rm "$R/src/untracked.cpp" "$R/include/a.h"
    deleted=$(stamp)
    [ "$deleted" != "$clean" ]
    [ "$deleted" != "$untracked" ]
}

@test "run from a subdirectory it measures the whole tree" {
    [ "$(stamp "" src)" = "$(stamp)" ]
}

@test "outside a git work tree it exits 1 and prints nothing" {
    mkdir -p "$BATS_TEST_TMPDIR/plain"
    run bash -c "cd '$BATS_TEST_TMPDIR/plain' && GIT_CEILING_DIRECTORIES='$BATS_TEST_TMPDIR' '$STAMP'"
    [ "$status" -eq 1 ]
    [ -z "$output" ]
}

@test "a base the repository does not have exits 2 and prints nothing" {
    run stamp 0123456789abcdef0123456789abcdef01234567
    [ "$status" -eq 2 ]
    [ -z "$output" ]
}
