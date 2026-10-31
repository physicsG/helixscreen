#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/ccache-shared-config.sh: the settings that let trees at different
# absolute paths share one ccache, and the check that says when they cannot.
# Each test runs the real ccache against a cache of its own.

SCRIPT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)/scripts/ccache-shared-config.sh"

setup() {
    load helpers
    command -v ccache >/dev/null || skip "ccache is not installed"
    unset CCACHE_CONFIGPATH CCACHE_BASEDIR CCACHE_HASHDIR CCACHE_NOHASHDIR CCACHE_MAXSIZE
    export CCACHE_DIR="$BATS_TEST_TMPDIR/cache"
    mkdir -p "$CCACHE_DIR"
}

conf() { ccache --get-config "$1"; }

@test "apply on a cache with no config sets base_dir, hash_dir and the max_size floor" {
    run "$SCRIPT" apply /work 200G
    [ "$status" -eq 0 ] || { echo "$output" >&2; return 1; }
    [ "$(conf base_dir)" = /work ]
    [ "$(conf hash_dir)" = false ]
    grep -qx 'max_size = 200G' "$CCACHE_DIR/ccache.conf"
}

@test "a second apply leaves the config byte-for-byte alone" {
    "$SCRIPT" apply /work 200G >/dev/null
    cp "$CCACHE_DIR/ccache.conf" "$BATS_TEST_TMPDIR/first.conf"
    run "$SCRIPT" apply /work 200G
    [ "$status" -eq 0 ]
    cmp "$CCACHE_DIR/ccache.conf" "$BATS_TEST_TMPDIR/first.conf"
    lacks "(was" "$output"
}

@test "apply raises a smaller max_size but never lowers a larger or unlimited one" {
    ccache --set-config max_size=5G
    "$SCRIPT" apply /work 200G >/dev/null
    grep -qx 'max_size = 200G' "$CCACHE_DIR/ccache.conf"

    ccache --set-config max_size=500G
    "$SCRIPT" apply /work 200G >/dev/null
    grep -qx 'max_size = 500G' "$CCACHE_DIR/ccache.conf"

    # A binary unit below the floor in bytes, though its number is larger.
    ccache --set-config max_size=190Gi
    "$SCRIPT" apply /work 210G >/dev/null
    grep -qx 'max_size = 210G' "$CCACHE_DIR/ccache.conf"

    ccache --set-config max_size=0
    "$SCRIPT" apply /work 200G >/dev/null
    grep -qx 'max_size = 0' "$CCACHE_DIR/ccache.conf"
}

@test "apply keeps a base_dir that covers the prefix and replaces one that does not" {
    ccache --set-config base_dir=/home
    "$SCRIPT" apply /home/me >/dev/null
    [ "$(conf base_dir)" = /home ]

    # A string prefix that is not a path prefix covers nothing.
    ccache --set-config base_dir=/work
    "$SCRIPT" apply /workspace >/dev/null
    [ "$(conf base_dir)" = /workspace ]
}

@test "apply turns hash_dir off when it is on" {
    ccache --set-config hash_dir=true
    "$SCRIPT" apply /work >/dev/null
    [ "$(conf hash_dir)" = false ]
}

@test "check passes when base_dir covers the source tree and hash_dir is off" {
    "$SCRIPT" apply /work >/dev/null
    run "$SCRIPT" check /work/trees/tree-a
    [ "$status" -eq 0 ]
    [ -z "$output" ]
}

@test "check warns when base_dir is not a path prefix of the source tree" {
    "$SCRIPT" apply /work >/dev/null
    run "$SCRIPT" check /workspace/tree-a
    [ "$status" -eq 1 ]
    contains "base_dir '/work' is not a prefix of /workspace/tree-a" "$output"
}

@test "check reads the effective base_dir, environment included" {
    "$SCRIPT" apply /work >/dev/null
    CCACHE_BASEDIR=/elsewhere run "$SCRIPT" check /work/trees/tree-a
    [ "$status" -eq 1 ]
    contains "base_dir '/elsewhere'" "$output"
}

@test "check warns on an unset base_dir and on hash_dir left on" {
    run "$SCRIPT" check /work/trees/tree-a
    [ "$status" -eq 1 ]
    contains "base_dir 'unset'" "$output"
    contains "hash_dir is 'true'" "$output"
}
