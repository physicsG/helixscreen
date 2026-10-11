#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for scripts/generate-manifest.sh
# Verifies manifest JSON generation from release tarballs.

load helpers

SCRIPT="scripts/generate-manifest.sh"

setup() {
    # Create temp directory with test tarballs
    TEST_DIR="$(mktemp -d)"
    # Create dummy tarballs for each platform
    dd if=/dev/zero bs=1024 count=1 2>/dev/null | gzip > "$TEST_DIR/helixscreen-pi-v0.9.5.tar.gz"
    dd if=/dev/zero bs=1024 count=1 2>/dev/null | gzip > "$TEST_DIR/helixscreen-pi32-v0.9.5.tar.gz"
    dd if=/dev/zero bs=1024 count=1 2>/dev/null | gzip > "$TEST_DIR/helixscreen-ad5m-v0.9.5.tar.gz"
    dd if=/dev/zero bs=1024 count=1 2>/dev/null | gzip > "$TEST_DIR/helixscreen-k1-v0.9.5.tar.gz"
}

teardown() {
    rm -rf "$TEST_DIR"
}

@test "generate-manifest.sh passes shellcheck" {
    if ! command -v shellcheck &>/dev/null; then
        skip "shellcheck not installed"
    fi
    shellcheck "$SCRIPT"
}

@test "generate-manifest.sh has valid bash syntax" {
    bash -n "$SCRIPT"
}

@test "generate-manifest.sh --help shows usage" {
    run bash "$SCRIPT" --help
    [ "$status" -eq 0 ]
    [[ "$output" == *"Usage"* ]]
}

@test "generates valid JSON with all platforms" {
    run bash "$SCRIPT" \
        --version "0.9.5" \
        --tag "v0.9.5" \
        --notes "Test release" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [ -f "$TEST_DIR/manifest.json" ]

    # Validate JSON structure
    run jq -e '.version' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [ "$output" = '"0.9.5"' ]

    run jq -e '.tag' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [ "$output" = '"v0.9.5"' ]

    run jq -e '.notes' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [ "$output" = '"Test release"' ]
}

@test "manifest includes all four platforms" {
    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    # Check each platform exists
    for plat in pi pi32 ad5m k1; do
        run jq -e ".assets.${plat}" "$TEST_DIR/manifest.json"
        [ "$status" -eq 0 ]
    done
}

@test "manifest includes SHA256 hashes" {
    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    # SHA256 hashes should be non-empty 64-char hex strings
    for plat in pi pi32 ad5m k1; do
        run jq -re ".assets.${plat}.sha256" "$TEST_DIR/manifest.json"
        [ "$status" -eq 0 ]
        [ "${#output}" -eq 64 ]
    done
}

@test "manifest includes correct URLs" {
    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    run jq -re '.assets.pi.url' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [[ "$output" == "https://releases.helixscreen.org/dev/helixscreen-pi-v0.9.5.tar.gz" ]]
}

@test "zip_url is emitted by default when a .zip is present; url stays tar.gz" {
    printf 'PK\003\004dummyzip' > "$TEST_DIR/helixscreen-pi.zip"
    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    # Legacy url still points at the tarball — pre-v0.99.31 clients read this.
    run jq -re '.assets.pi.url' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [[ "$output" == *"helixscreen-pi-v0.9.5.tar.gz" ]] || fail "url does not end in the versioned pi tarball: $output"

    # zip_url is the preferred asset for v0.99.31+ clients.
    run jq -re '.assets.pi.zip_url' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [ "$output" = "https://releases.helixscreen.org/dev/helixscreen-pi.zip" ]

    run jq -re '.assets.pi.zip_sha256' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [ "${#output}" -eq 64 ]
}

@test "the ESP32 firmware zip never becomes a manifest platform" {
    printf 'PK\003\004dummyzip' > "$TEST_DIR/helixscreen-esp32-ktouch-v0.9.5.zip"
    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    run jq -r '.assets | keys | join(" ")' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [ "$output" = "ad5m k1 pi pi32" ] || fail "manifest platforms: $output"
}

@test "--no-include-zip suppresses zip_url even when a .zip is present" {
    printf 'PK\003\004dummyzip' > "$TEST_DIR/helixscreen-pi.zip"
    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json" --no-include-zip

    run jq -e '.assets.pi.zip_url' "$TEST_DIR/manifest.json"
    [ "$status" -ne 0 ]
    # Legacy tarball url is unaffected by suppression.
    run jq -re '.assets.pi.url' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [[ "$output" == *"helixscreen-pi-v0.9.5.tar.gz" ]]
}

@test "no zip_url when a platform has no .zip (default on, skips gracefully)" {
    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    run jq -e '.assets.pi.zip_url' "$TEST_DIR/manifest.json"
    [ "$status" -ne 0 ]
}

#
# Per-platform zip gate (prestonbrown/helixscreen#993)
#
# Pre-v0.99.102 in-app updaters verify a downloaded zip with `unzip -tqq`.
# BusyBox only grew `unzip -t` in 1.32, and the K2 ships no unzip at all, so
# those clients reject an intact zip as "Corrupt download" and can never reach
# the release that fixes them. Serving those platforms the tar.gz keeps the
# self-update path alive. The gate is the ONLY lever that reaches an already
# deployed binary — the client-side fix cannot bootstrap itself, and removing a
# platform is one-way for anything still below v0.99.102.
#

@test "zip_url is gated off by default for BusyBox platforms" {
    printf 'PK\003\004dummyzip' > "$TEST_DIR/helixscreen-pi.zip"
    printf 'PK\003\004dummyzip' > "$TEST_DIR/helixscreen-ad5m.zip"

    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    # ad5m must NOT be offered a zip.
    run jq -e '.assets.ad5m.zip_url' "$TEST_DIR/manifest.json"
    [ "$status" -ne 0 ]

    # pi has a real unzip and keeps the preferred zip asset.
    run jq -re '.assets.pi.zip_url' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [[ "$output" == "https://releases.helixscreen.org/dev/helixscreen-pi.zip" ]]
}

@test "gated platforms still get a complete tar.gz asset" {
    printf 'PK\003\004dummyzip' > "$TEST_DIR/helixscreen-ad5m.zip"

    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    # A gated platform is not a dropped platform — the tar.gz must be intact,
    # or the client has nothing at all to download.
    run jq -re '.assets.ad5m.url' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [ "$output" = "https://releases.helixscreen.org/dev/helixscreen-ad5m-v0.9.5.tar.gz" ]

    run jq -re '.assets.ad5m.sha256' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [ "${#output}" -eq 64 ]

    run jq -re '.assets.ad5m.size' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [ "$output" -gt 0 ]
}

@test "default gate covers every BusyBox/OpenWrt platform in the release matrix" {
    for plat in ad5m ad5x cc1 k1 k2 snapmaker-u1; do
        dd if=/dev/zero bs=1024 count=1 2>/dev/null | gzip \
            > "$TEST_DIR/helixscreen-${plat}-v0.9.5.tar.gz"
        printf 'PK\003\004dummyzip' > "$TEST_DIR/helixscreen-${plat}.zip"
    done

    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    for plat in ad5m ad5x cc1 k1 k2 snapmaker-u1; do
        run jq -e ".assets[\"${plat}\"].zip_url" "$TEST_DIR/manifest.json"
        [ "$status" -ne 0 ] || fail "$plat is offered a zip its fleet cannot verify"
    done
}

@test "--zip-exclude replaces the default gate list" {
    printf 'PK\003\004dummyzip' > "$TEST_DIR/helixscreen-pi.zip"
    printf 'PK\003\004dummyzip' > "$TEST_DIR/helixscreen-ad5m.zip"

    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json" \
        --zip-exclude "pi"

    # pi is now gated...
    run jq -e '.assets.pi.zip_url' "$TEST_DIR/manifest.json"
    [ "$status" -ne 0 ]
    # ...and ad5m is not, although the built-in list holds it: the flag REPLACES.
    run jq -re '.assets.ad5m.zip_url' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [[ "$output" == "https://releases.helixscreen.org/dev/helixscreen-ad5m.zip" ]]
}

@test "--zip-exclude '' re-enables zip for every platform" {
    printf 'PK\003\004dummyzip' > "$TEST_DIR/helixscreen-k1.zip"
    printf 'PK\003\004dummyzip' > "$TEST_DIR/helixscreen-ad5m.zip"

    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json" \
        --zip-exclude ""

    run jq -re '.assets.k1.zip_url' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    run jq -re '.assets.ad5m.zip_url' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
}

@test "--no-include-zip beats --zip-exclude and suppresses zip everywhere" {
    printf 'PK\003\004dummyzip' > "$TEST_DIR/helixscreen-pi.zip"
    printf 'PK\003\004dummyzip' > "$TEST_DIR/helixscreen-k1.zip"

    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json" \
        --zip-exclude "" --no-include-zip

    run jq -e '.assets.pi.zip_url' "$TEST_DIR/manifest.json"
    [ "$status" -ne 0 ]
    run jq -e '.assets.k1.zip_url' "$TEST_DIR/manifest.json"
    [ "$status" -ne 0 ]
}

@test "--no-include-zip refuses to drop a zip-only platform" {
    # sonic-pad ships a zip and no tarball, and is outside the gate list, so the
    # flag alone is what leaves nothing to serve it. A quiet exit 0 would emit a
    # manifest missing a whole platform.
    printf 'PK\003\004dummyzip' > "$TEST_DIR/helixscreen-sonic-pad.zip"

    run bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json" \
        --no-include-zip
    [ "$status" -ne 0 ]
    contains "sonic-pad" "$output"
    contains "nothing to serve" "$output"
}

@test "gated platforms are reported on stdout, never silently dropped" {
    printf 'PK\003\004dummyzip' > "$TEST_DIR/helixscreen-ad5m.zip"

    run bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    # A gate that hides what it dropped reads as "everything shipped".
    [[ "$output" == *"zip gated off"* ]] || fail "gate said nothing: $output"
    contains "ad5m" "$output"
}

@test "no zip gate noise when the gated platform has no .zip at all" {
    # ad5m tarball exists but no ad5m zip — nothing withheld, nothing to report.
    run bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [[ "$output" != *"gated"* ]]
}

@test "manifest includes published_at timestamp" {
    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    run jq -re '.published_at' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    # Should be ISO 8601 format
    [[ "$output" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}T ]]
}

@test "handles subset of platforms" {
    # Remove some tarballs
    rm "$TEST_DIR/helixscreen-ad5m-v0.9.5.tar.gz"
    rm "$TEST_DIR/helixscreen-k1-v0.9.5.tar.gz"

    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Pi only" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    # pi and pi32 should exist
    run jq -e '.assets.pi' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    run jq -e '.assets.pi32' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]

    # ad5m and k1 should NOT exist
    run jq -e '.assets.ad5m' "$TEST_DIR/manifest.json"
    [ "$status" -ne 0 ]
    run jq -e '.assets.k1' "$TEST_DIR/manifest.json"
    [ "$status" -ne 0 ]
}

@test "fails with no tarballs in directory" {
    local empty_dir
    empty_dir="$(mktemp -d)"

    run bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Empty" \
        --dir "$empty_dir" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$empty_dir/manifest.json"
    [ "$status" -ne 0 ]

    rm -rf "$empty_dir"
}

@test "fails with missing --version" {
    run bash "$SCRIPT" \
        --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"
    [ "$status" -ne 0 ]
}

@test "fails with missing --dir" {
    run bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"
    [ "$status" -ne 0 ]
}

@test "fails with missing --base-url" {
    run bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --output "$TEST_DIR/manifest.json"
    [ "$status" -ne 0 ]
}

@test "fails with missing --output" {
    run bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev"
    [ "$status" -ne 0 ]
}

@test "prerelease versions and hyphenated platforms both reach the manifest" {
    PRE_DIR="$(mktemp -d)"
    for plat in pi snapmaker-u1 android-arm64; do
        dd if=/dev/zero bs=1024 count=1 2>/dev/null | gzip \
            > "$PRE_DIR/helixscreen-${plat}-v1.1.0-beta.1.tar.gz"
    done

    run bash "$SCRIPT" \
        --version "1.1.0-beta.1" --tag "v1.1.0-beta.1" --notes "Beta" \
        --dir "$PRE_DIR" \
        --base-url "https://releases.helixscreen.org/beta" \
        --output "$PRE_DIR/manifest.json"
    [ "$status" -eq 0 ] || fail "generate-manifest.sh exited $status: $output"

    for plat in pi snapmaker-u1 android-arm64; do
        run jq -re --arg p "$plat" '.assets[$p].url' "$PRE_DIR/manifest.json"
        [ "$status" -eq 0 ] || fail "$plat missing from the manifest"
        [ "$output" = "https://releases.helixscreen.org/beta/helixscreen-${plat}-v1.1.0-beta.1.tar.gz" ] \
            || fail "$plat url wrong: $output"
    done

    rm -rf "$PRE_DIR"
}

@test "a numeric prerelease identifier does not get mistaken for the version" {
    NUM_DIR="$(mktemp -d)"
    dd if=/dev/zero bs=1024 count=1 2>/dev/null | gzip \
        > "$NUM_DIR/helixscreen-pi-v1.1.0-2.tar.gz"

    run bash "$SCRIPT" \
        --version "1.1.0-2" --tag "v1.1.0-2" --notes "Numeric prerelease" \
        --dir "$NUM_DIR" \
        --base-url "https://releases.helixscreen.org/beta" \
        --output "$NUM_DIR/manifest.json"
    [ "$status" -eq 0 ] || fail "generate-manifest.sh exited $status: $output"

    # The platform key must be "pi", not "pi-v1.1.0" with "2" as the version.
    run jq -re '.assets | keys | join(",")' "$NUM_DIR/manifest.json"
    [ "$output" = "pi" ] || fail "platform key wrong: [$output]"

    rm -rf "$NUM_DIR"
}

@test "a platform prefix does not select a longer platform's tarball" {
    # k1-dynamic shares the helixscreen-k1- prefix and sorts first, so a
    # prefix glob would hand the k1 manifest entry the dynamic artifact. The
    # selection must re-parse the platform half to an exact match.
    local trap_dir
    trap_dir="$(mktemp -d)"
    dd if=/dev/zero bs=1024 count=1 2>/dev/null | gzip \
        > "$trap_dir/helixscreen-k1-dynamic-v0.99.31.tar.gz"
    echo dynamic-bytes > "$trap_dir/dynamic-marker"
    tar -czf "$trap_dir/helixscreen-k1-dynamic-v0.99.31.tar.gz" \
        -C "$trap_dir" dynamic-marker
    dd if=/dev/zero bs=1024 count=1 2>/dev/null | gzip \
        > "$trap_dir/helixscreen-k1-v1.1.0.tar.gz"
    echo k1-bytes > "$trap_dir/k1-marker"
    tar -czf "$trap_dir/helixscreen-k1-v1.1.0.tar.gz" \
        -C "$trap_dir" k1-marker

    run bash "$SCRIPT" \
        --version "1.1.0" --tag "v1.1.0" --notes "Prefix trap" \
        --dir "$trap_dir" \
        --base-url "https://releases.helixscreen.org/stable" \
        --output "$trap_dir/manifest.json"
    [ "$status" -eq 0 ] || fail "generate-manifest.sh exited $status: $output"

    # k1 must resolve to its own tarball, not the k1-dynamic one.
    run jq -re '.assets.k1.url' "$trap_dir/manifest.json"
    [ "$status" -eq 0 ] || fail "k1 entry missing"
    [ "$output" = "https://releases.helixscreen.org/stable/helixscreen-k1-v1.1.0.tar.gz" ] \
        || fail "k1 url selected the wrong tarball: $output"

    # And k1-dynamic stays its own platform entry, uncontaminated.
    run jq -re '.assets["k1-dynamic"].url' "$trap_dir/manifest.json"
    [ "$status" -eq 0 ] || fail "k1-dynamic entry missing"
    [ "$output" = "https://releases.helixscreen.org/stable/helixscreen-k1-dynamic-v0.99.31.tar.gz" ] \
        || fail "k1-dynamic url wrong: $output"

    # The two entries must carry different digests (different content).
    local k1_sha dyn_sha
    k1_sha=$(jq -re '.assets.k1.sha256' "$trap_dir/manifest.json")
    dyn_sha=$(jq -re '.assets["k1-dynamic"].sha256' "$trap_dir/manifest.json")
    [ "$k1_sha" != "$dyn_sha" ] || fail "k1 and k1-dynamic share a digest"

    rm -rf "$trap_dir"
}

@test "unified mips aliases: k1/ad5x entries match the mips tarball byte-for-byte" {
    # The unified MIPS release drops one archive copied under three names. The
    # manifest must carry all three keys with identical digests, so binaries
    # reporting retired keys keep updating from the same bytes.
    local alias_dir
    alias_dir="$(mktemp -d)"
    echo payload > "$alias_dir/payload"
    tar -czf "$alias_dir/helixscreen-mips-v1.1.0.tar.gz" -C "$alias_dir" payload
    cp "$alias_dir/helixscreen-mips-v1.1.0.tar.gz" "$alias_dir/helixscreen-k1-v1.1.0.tar.gz"
    cp "$alias_dir/helixscreen-mips-v1.1.0.tar.gz" "$alias_dir/helixscreen-ad5x-v1.1.0.tar.gz"

    run bash "$SCRIPT" \
        --version "1.1.0" --tag "v1.1.0" --notes "Unified MIPS" \
        --dir "$alias_dir" \
        --base-url "https://releases.helixscreen.org/stable" \
        --output "$alias_dir/manifest.json"
    [ "$status" -eq 0 ] || fail "generate-manifest.sh exited $status: $output"

    local mips_sha
    mips_sha=$(jq -re '.assets.mips.sha256' "$alias_dir/manifest.json")
    for key in k1 ad5x; do
        run jq -re --arg k "$key" '.assets[$k].sha256' "$alias_dir/manifest.json"
        [ "$status" -eq 0 ] || fail "$key entry missing from the manifest"
        [ "$output" = "$mips_sha" ] || fail "$key digest differs from mips"
    done

    rm -rf "$alias_dir"
}

#
# Zip-only platforms
#
# The zip carries no version (helixscreen-{platform}.zip), so a platform can be
# discovered from either asset. A missing tar.gz drops only the legacy
# url/sha256/size fields; the zip asset still reaches the manifest.
#

@test "a zip-only platform reaches the manifest with zip fields and no legacy url" {
    local zip_dir
    zip_dir="$(mktemp -d)"
    printf 'PK\003\004zip-only-pi' > "$zip_dir/helixscreen-pi.zip"

    run bash "$SCRIPT" \
        --version "1.2.3" --tag "v1.2.3" --notes "Zip only" \
        --dir "$zip_dir" \
        --base-url "https://releases.helixscreen.org/stable" \
        --output "$zip_dir/manifest.json"
    [ "$status" -eq 0 ] || fail "generate-manifest.sh exited $status: $output"

    run jq -re '.assets.pi.zip_url' "$zip_dir/manifest.json"
    [ "$status" -eq 0 ] || fail "pi missing from the manifest"
    [ "$output" = "https://releases.helixscreen.org/stable/helixscreen-pi.zip" ] \
        || fail "zip_url wrong: $output"

    run jq -re '.assets.pi.zip_sha256' "$zip_dir/manifest.json"
    [ "${#output}" -eq 64 ] || fail "zip_sha256 wrong length: $output"
    run jq -re '.assets.pi.zip_size' "$zip_dir/manifest.json"
    [ "$output" -gt 0 ] || fail "zip_size not positive: $output"

    # The legacy fields describe a tar.gz that does not exist — they must be absent
    # rather than present and pointing at nothing.
    for field in url sha256 size; do
        run jq -e ".assets.pi.${field}" "$zip_dir/manifest.json"
        [ "$status" -ne 0 ] || fail "legacy $field emitted for a zip-only platform"
    done

    rm -rf "$zip_dir"
}

@test "a hyphenated platform key survives zip-only discovery intact" {
    # There is no version half to split off a zip name, so the whole key —
    # hyphens included — is the platform.
    local zip_dir
    zip_dir="$(mktemp -d)"
    printf 'PK\003\004dyn' > "$zip_dir/helixscreen-k1-dynamic.zip"

    run bash "$SCRIPT" \
        --version "1.2.3" --tag "v1.2.3" --notes "Hyphens" \
        --dir "$zip_dir" \
        --base-url "https://releases.helixscreen.org/stable" \
        --output "$zip_dir/manifest.json"
    [ "$status" -eq 0 ] || fail "generate-manifest.sh exited $status: $output"

    run jq -re '.assets | keys | join(",")' "$zip_dir/manifest.json"
    [ "$output" = "k1-dynamic" ] || fail "platform key wrong: [$output]"

    rm -rf "$zip_dir"
}

@test "a tarball-only platform carries the legacy fields and no zip fields" {
    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    run jq -rc '.assets.pi | keys_unsorted | join(",")' "$TEST_DIR/manifest.json"
    [ "$output" = "url,sha256,size" ] || fail "tarball-only field set changed: $output"
}

@test "a platform with both assets emits the same fields in the same order" {
    # Zip-only support must change nothing that ships today: while both assets
    # exist, the entry is field-for-field what it has always been.
    printf 'PK\003\004dummyzip' > "$TEST_DIR/helixscreen-pi.zip"
    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    run jq -rc '.assets.pi | keys_unsorted | join(",")' "$TEST_DIR/manifest.json"
    [ "$output" = "url,sha256,size,zip_url,zip_sha256,zip_size" ] \
        || fail "combined field order changed: $output"
}

@test "a directory with neither asset exits non-zero" {
    local bare_dir
    bare_dir="$(mktemp -d)"
    # Files that are not release assets must not be mistaken for one.
    echo unrelated > "$bare_dir/README.txt"
    echo unrelated > "$bare_dir/helixscreen-notes.txt"

    run bash "$SCRIPT" \
        --version "1.2.3" --tag "v1.2.3" --notes "Nothing" \
        --dir "$bare_dir" \
        --base-url "https://releases.helixscreen.org/stable" \
        --output "$bare_dir/manifest.json"
    [ "$status" -ne 0 ] || fail "a manifest was produced from no assets"

    rm -rf "$bare_dir"
}

@test "a gated platform with only a zip is a hard error, never a dropped platform" {
    # Withholding the zip is safe only while a tar.gz remains to serve instead.
    # With neither, the platform would vanish from the manifest and its clients
    # would stop being offered any update at all.
    local gated_dir
    gated_dir="$(mktemp -d)"
    printf 'PK\003\004dummyzip' > "$gated_dir/helixscreen-ad5m.zip"

    run bash "$SCRIPT" \
        --version "1.2.3" --tag "v1.2.3" --notes "Stranded" \
        --dir "$gated_dir" \
        --base-url "https://releases.helixscreen.org/stable" \
        --output "$gated_dir/manifest.json"
    [ "$status" -ne 0 ] || fail "ad5m silently dropped out of the manifest"
    [[ "$output" == *"ad5m"* ]] || fail "the error does not name the platform: $output"

    rm -rf "$gated_dir"
}
