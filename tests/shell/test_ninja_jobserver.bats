#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/ninja-jobserver.sh hands pool-docker.sh a ninja new enough to join a
# jobserver, fetched once against a pinned sha256 and cached, or exits 1 so the
# caller keeps its fixed share. Every fetch here reads a local zip through
# file://; no test reaches the network.

bats_require_minimum_version 1.5.0
load helpers

REPO="$(cd "${BATS_TEST_DIRNAME}/../.." && pwd)"
NJ="$REPO/scripts/ninja-jobserver.sh"

setup() {
    export HELIX_NINJA_CACHE="$BATS_TEST_TMPDIR/cache"
    unset HELIX_NINJA_URL HELIX_NINJA_SHA256
}

# release_zip VERSION [MEMBER]: a release zip whose MEMBER (default ninja)
# reports VERSION; points the script at it with its true sha256.
release_zip() {
    local zip="$BATS_TEST_TMPDIR/ninja-$1.zip"
    python3 -I -c '
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1], "w")
z.writestr(sys.argv[3], "#!/bin/sh\necho %s\n" % sys.argv[2])
z.close()' "$zip" "$1" "${2:-ninja}"
    export HELIX_NINJA_URL="file://$zip"
    HELIX_NINJA_SHA256=$(sha256sum "$zip" | cut -d' ' -f1)
    export HELIX_NINJA_SHA256
}

@test "fetches a pinned ninja 1.13 once, then works offline from the cache" {
    release_zip 1.13.2
    run --separate-stderr "$NJ"
    [ "$status" -eq 0 ]
    [ "$output" = "$HELIX_NINJA_CACHE/ninja" ]
    [ "$("$output" --version)" = 1.13.2 ]
    export HELIX_NINJA_URL="file://$BATS_TEST_TMPDIR/gone.zip"
    run --separate-stderr "$NJ"
    [ "$status" -eq 0 ]
    [ "$output" = "$HELIX_NINJA_CACHE/ninja" ]
    # Nothing but the binary is left in the cache.
    [ "$(ls -A "$HELIX_NINJA_CACHE")" = ninja ]
}

@test "a zip whose sha256 does not match is refused and nothing is cached" {
    release_zip 1.13.2
    export HELIX_NINJA_SHA256=0000000000000000000000000000000000000000000000000000000000000000
    run --separate-stderr "$NJ"
    [ "$status" -eq 1 ]
    [ -z "$output" ]
    contains "sha256" "$stderr"
    [ -z "$(ls -A "$HELIX_NINJA_CACHE")" ]
}

@test "no network, a zip with no ninja, or a ninja older than 1.13 all exit 1" {
    export HELIX_NINJA_URL="file://$BATS_TEST_TMPDIR/gone.zip"
    run --separate-stderr "$NJ"
    [ "$status" -eq 1 ]
    contains "cannot fetch" "$stderr"
    release_zip 1.13.2 bin/ninja
    run --separate-stderr "$NJ"
    [ "$status" -eq 1 ]
    contains "holds no ninja" "$stderr"
    release_zip 1.12.1
    run --separate-stderr "$NJ"
    [ "$status" -eq 1 ]
    [ -z "$(ls -A "$HELIX_NINJA_CACHE")" ]
}

@test "a cached ninja older than 1.13 is replaced by a fresh fetch" {
    mkdir -p "$HELIX_NINJA_CACHE"
    printf '#!/bin/sh\necho 1.11.1\n' > "$HELIX_NINJA_CACHE/ninja"
    chmod +x "$HELIX_NINJA_CACHE/ninja"
    release_zip 1.13.2
    run --separate-stderr "$NJ"
    [ "$status" -eq 0 ]
    [ "$("$HELIX_NINJA_CACHE/ninja" --version)" = 1.13.2 ]
}

@test "a host with no upstream ninja build exits 1 without fetching" {
    mock_command_script uname 'case "$1" in -s) echo Darwin ;; -m) echo arm64 ;; esac'
    release_zip 1.13.2
    PATH="$BATS_TEST_TMPDIR/bin:$PATH" run --separate-stderr "$NJ"
    [ "$status" -eq 1 ]
    contains "Darwin/arm64" "$stderr"
    [ ! -e "$HELIX_NINJA_CACHE" ]
}
