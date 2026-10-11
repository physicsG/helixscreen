#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/check-deps.sh: libusb is a hard requirement on Linux (the native
# build links -lusb-1.0 unconditionally, so a missing header otherwise fails
# thirty minutes into the compile), optional on macOS, and not the checker's
# business under --minimal. ALSA is a warning on Linux: the build survives
# without it, but the sound backend is compiled out.
#
# pkg-config and uname are stubbed on PATH so the assertions do not depend on
# what the test host has installed.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"
CHECK_DEPS="$WORKTREE_ROOT/scripts/check-deps.sh"

setup() {
    load helpers
    cd "$WORKTREE_ROOT" || return 1
    # Everything else the checker asks pkg-config about goes to the real one.
    PKG_STUB_REAL="$(command -v pkg-config || true)"
    export PKG_STUB_REAL
    export PKG_STUB_PRESENT="" PKG_STUB_ABSENT=""
    mock_command_script pkg-config '
op="$1"; pkg="$2"
for a in $PKG_STUB_ABSENT; do [ "$a" = "$pkg" ] && exit 1; done
for p in $PKG_STUB_PRESENT; do
    case "$p" in "$pkg"=*) [ "$op" = "--modversion" ] && echo "${p#*=}"; exit 0 ;; esac
done
[ -n "$PKG_STUB_REAL" ] && exec "$PKG_STUB_REAL" "$@"
exit 1
'
}

pretend_os() {
    export STUB_UNAME_S="$1"
    mock_command_script uname 'case "$1" in -s) echo "$STUB_UNAME_S" ;; *) echo x86_64 ;; esac'
}

# --- libusb ---------------------------------------------------------------

@test "Linux without libusb headers: check-deps fails and names the package" {
    pretend_os Linux
    export PKG_STUB_ABSENT="libusb-1.0"
    run "$CHECK_DEPS"
    [ "$status" -eq 1 ]
    contains "libusb-1.0 development headers not found" "$output"
    contains "libusb-1.0-0-dev" "$output"
    contains "Missing required dependencies:" "$output"
}

@test "Linux with libusb headers: reported found, never listed as missing" {
    pretend_os Linux
    export PKG_STUB_PRESENT="libusb-1.0=1.0.27"
    run "$CHECK_DEPS"
    contains "libusb found: 1.0.27" "$output"
    lacks "libusb-1.0 development headers not found" "$output"
}

@test "--minimal does not check libusb (cross targets own that dependency)" {
    pretend_os Linux
    export PKG_STUB_ABSENT="libusb-1.0"
    run "$CHECK_DEPS" --minimal
    lacks "libusb" "$output"
}

@test "macOS without libusb: optional, so a skip rather than a failure" {
    pretend_os Darwin
    mock_command sw_vers "14.0"
    export PKG_STUB_ABSENT="libusb-1.0"
    run "$CHECK_DEPS"
    contains "libusb not found (optional on macOS" "$output"
    lacks "libusb-1.0 development headers not found" "$output"
}

# --- ALSA -----------------------------------------------------------------

@test "Linux without ALSA headers: warns with the package name, does not fail on it" {
    pretend_os Linux
    export PKG_STUB_PRESENT="libusb-1.0=1.0.27"
    export PKG_STUB_ABSENT="alsa"
    run "$CHECK_DEPS"
    contains "ALSA development headers not found" "$output"
    contains "libasound2-dev" "$output"
    # A warning never lands in the required-dependency list.
    refute_sh 'printf "%s" "$output" | grep "Missing required dependencies:" | grep -q -i alsa'
}

@test "Linux with ALSA headers: reported found" {
    pretend_os Linux
    export PKG_STUB_PRESENT="libusb-1.0=1.0.27 alsa=1.2.11"
    run "$CHECK_DEPS"
    contains "ALSA found: 1.2.11" "$output"
    lacks "ALSA development headers not found" "$output"
}
