#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# extract-symbols (Makefile) writes the .sym and .debug that crash reports are
# symbolized with. Every cross build runs it, including a build that relinked
# nothing, when the binary on disk is already the stripped one. These tests pin
# that a rerun keeps the maps from the link, and that a binary the build cannot
# symbolize fails it instead of shipping an empty map.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

setup() {
    load helpers
    DIR="$BATS_TEST_TMPDIR/sym"
    mkdir -p "$DIR"
    {
        printf 'NM_CMD := nm\nOBJCOPY_CMD := objcopy\nREADELF_CMD := readelf\n'
        awk '/^define extract-symbols/,/^endef/' "$WORKTREE_ROOT/Makefile"
        printf 'symbols:\n\t$(call extract-symbols,app)\n'
    } > "$DIR/Makefile"
    printf 'int helix_known_function(void) { return 42; }\nint main(void) { return helix_known_function(); }\n' \
        > "$BATS_TEST_TMPDIR/app.c"
    cc -g -o "$DIR/app" "$BATS_TEST_TMPDIR/app.c"
}

extract() {
    run make --no-print-directory -C "$DIR" -f Makefile symbols
}

@test "a freshly linked binary yields a symbol map naming its functions" {
    extract
    [ "$status" -eq 0 ] || fail "$output"
    grep -q " T helix_known_function" "$DIR/app.sym"
    [ -s "$DIR/app.debug" ]
}

@test "rerunning after the binary was stripped keeps the maps from the link" {
    extract
    [ "$status" -eq 0 ] || fail "$output"
    strip "$DIR/app"
    extract
    [ "$status" -eq 0 ] || fail "$output"
    contains "already stripped" "$output"
    grep -q " T helix_known_function" "$DIR/app.sym"
}

@test "a stripped binary with no map from its link fails the build" {
    strip "$DIR/app"
    extract
    [ "$status" -ne 0 ] || fail "an unsymbolizable binary must fail: $output"
    contains "no symbol table" "$output"
}

@test "an empty map from a stripped binary fails the build" {
    strip "$DIR/app"
    : > "$DIR/app.sym"
    extract
    [ "$status" -ne 0 ] || fail "an empty map must fail: $output"
}
