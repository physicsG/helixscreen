#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for scripts/check-esp32-manifest.sh: the manifest's ESP32 entries agree with the
# zips on disk.

load helpers

SCRIPT="scripts/check-esp32-manifest.sh"

setup() {
    TEST_DIR="$(mktemp -d)"
    ZIP="$TEST_DIR/helixscreen-esp32-ktouch-v0.9.5.zip"
    printf 'PK\003\004dummyzip' > "$ZIP"
    SHA=$(sha256sum "$ZIP" | cut -d' ' -f1)
}

teardown() {
    rm -rf "$TEST_DIR"
}

manifest() {
    printf '{"assets":{"pi":{"url":"u","sha256":"x"}%s}}' "$1" > "$TEST_DIR/m.json"
}

@test "check-esp32-manifest.sh passes shellcheck" {
    command -v shellcheck &>/dev/null || skip "shellcheck not installed"
    shellcheck "$SCRIPT"
}

@test "a matching entry passes" {
    manifest ",\"esp32-ktouch\":{\"url\":\"https://h/p/helixscreen-esp32-ktouch-v0.9.5.zip\",\"sha256\":\"$SHA\"}"
    run bash "$SCRIPT" "$TEST_DIR/m.json" "$TEST_DIR"
    [ "$status" -eq 0 ]
}

@test "no zip and no entry passes" {
    rm "$ZIP"
    manifest ""
    run bash "$SCRIPT" "$TEST_DIR/m.json" "$TEST_DIR"
    [ "$status" -eq 0 ]
}

@test "a zip with no entry fails" {
    manifest ""
    run bash "$SCRIPT" "$TEST_DIR/m.json" "$TEST_DIR"
    [ "$status" -ne 0 ]
    [[ "$output" == *"no esp32-ktouch entry"* ]]
}

@test "a wrong sha256 fails" {
    manifest ",\"esp32-ktouch\":{\"url\":\"https://h/p/helixscreen-esp32-ktouch-v0.9.5.zip\",\"sha256\":\"$(printf '0%.0s' {1..64})\"}"
    run bash "$SCRIPT" "$TEST_DIR/m.json" "$TEST_DIR"
    [ "$status" -ne 0 ]
    [[ "$output" == *"does not match"* ]]
}

@test "a url for another file fails" {
    manifest ",\"esp32-ktouch\":{\"url\":\"https://h/p/other.zip\",\"sha256\":\"$SHA\"}"
    run bash "$SCRIPT" "$TEST_DIR/m.json" "$TEST_DIR"
    [ "$status" -ne 0 ]
    [[ "$output" == *"does not end in"* ]]
}

@test "an entry with no zip fails" {
    rm "$ZIP"
    manifest ",\"esp32-ktouch\":{\"url\":\"https://h/p/helixscreen-esp32-ktouch-v0.9.5.zip\",\"sha256\":\"$SHA\"}"
    run bash "$SCRIPT" "$TEST_DIR/m.json" "$TEST_DIR"
    [ "$status" -ne 0 ]
    [[ "$output" == *"built no matching zip"* ]]
}
