#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Tests for helix-launcher.sh display rung selection

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors for output
RED='\033[31m'
GREEN='\033[32m'
YELLOW='\033[33m'
CYAN='\033[36m'
BOLD='\033[1m'
RESET='\033[0m'

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0
declare -a FAILED_TESTS

print_test() {
    TESTS_RUN=$((TESTS_RUN + 1))
    echo -e "\n${CYAN}[TEST $TESTS_RUN]${RESET} $1"
}

assert() {
    local condition="$1"
    local message="$2"
    if eval "$condition"; then
        echo -e "${GREEN}  ✓${RESET} $message"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        echo -e "${RED}  ✗${RESET} $message"
        FAILED_TESTS+=("TEST $TESTS_RUN: $message")
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

# Create temp dir with mock binaries for testing
setup_mock_bindir() {
    local tmpdir
    tmpdir=$(mktemp -d)
    echo "$tmpdir"
}

cleanup_mock_bindir() {
    rm -rf "$1"
}

# Extract the named functions from the launcher, so a harness exercises the
# real code. Name everything the function under test calls: a helper left out
# does not fail the harness, the shell reports "not found", returns 127, and the
# caller reads that as an ordinary false — which looks exactly like a decision.
extract_launcher_functions() {
    local launcher="${PROJECT_ROOT}/scripts/helix-launcher.sh"
    local fn
    for fn in "$@"; do
        sed -n "/^${fn}()/,/^}/p" "$launcher"
    done
}

# Build a mock helix-screen whose --probe-egl prints a verdict and exits with
# the given code. Every probe leaves a mark, so a test can prove none ran.
make_mock_primary() {
    local path="$1"
    local rc="$2"
    local verdict="$3"
    cat > "$path" << EOF
#!/bin/sh
if [ "\$1" = "--probe-egl" ]; then
    echo probed >> "$path.probes"
    echo "$verdict"
    exit $rc
fi
EOF
    chmod +x "$path"
}

# A /dev/dri stand-in, with or without a card node.
make_dri_dir() {
    local bindir="$1"
    local with_card="$2"
    mkdir -p "$bindir/dri"
    if [ "$with_card" = yes ]; then
        : > "$bindir/dri/card0"
    fi
    echo "$bindir/dri"
}

# Create a harness that prints the backend the launcher hands the app
create_backend_harness() {
    local bindir="$1"
    local harness="$bindir/_backend_harness.sh"
    echo '#!/bin/sh' > "$harness"
    extract_launcher_functions log probe_egl has_dri_card app_display_backend >> "$harness"
    # shellcheck disable=SC2016 # the harness expands its own arguments
    echo 'app_display_backend "$1"' >> "$harness"
    chmod +x "$harness"
    echo "$harness"
}

# Run the harness against a mock whose probe exits RC, on a system with or
# without a DRM card, with HELIX_DISPLAY_BACKEND as given (empty = unset).
backend_for() {
    local bindir="$1" rc="$2" card="$3" forced="$4"
    make_mock_primary "$bindir/helix-screen" "$rc" "mock verdict"
    local dri harness
    dri=$(make_dri_dir "$bindir" "$card")
    harness=$(create_backend_harness "$bindir")
    if [ -n "$forced" ]; then
        HELIX_DRI_DIR="$dri" HELIX_DISPLAY_BACKEND="$forced" \
            sh "$harness" "$bindir/helix-screen" 2>/dev/null
    else
        HELIX_DRI_DIR="$dri" sh "$harness" "$bindir/helix-screen" 2>/dev/null
    fi
}

probe_count() {
    if [ -f "$1/helix-screen.probes" ]; then
        wc -l < "$1/helix-screen.probes" | tr -d ' '
    else
        echo 0
    fi
}

test_probe_ok_tells_app_egl() {
    print_test "EGL probe succeeds → app is told egl"
    local bindir result
    bindir=$(setup_mock_bindir)
    result=$(backend_for "$bindir" 0 yes "")
    assert "[ '$result' = 'egl' ]" "EGL rung taken when the probe reports a hardware renderer"
    cleanup_mock_bindir "$bindir"
}

# Demoting two rungs on a GPU failure would hide a working middle rung.
test_probe_declines_tells_app_drm_not_fbdev() {
    print_test "EGL probe declines → app is told drm, not fbdev"
    local bindir result
    bindir=$(setup_mock_bindir)
    result=$(backend_for "$bindir" 1 yes "")
    assert "[ '$result' = 'drm' ]" "DRM dumb buffers when the probe declined"
    cleanup_mock_bindir "$bindir"
}

test_probe_no_rungs_leaves_auto() {
    print_test "Binary without rungs → app auto-detects"
    local bindir result
    bindir=$(setup_mock_bindir)
    result=$(backend_for "$bindir" 2 yes "")
    assert "[ -z '$result' ]" "No backend passed on when the binary has no rungs"
    cleanup_mock_bindir "$bindir"
}

# A DRM card without libdrm: the app falls back to fbdev in-process, and the
# watchdog must see fbdev to keep the splash up while it loads.
test_probe_no_drm_tells_app_fbdev() {
    print_test "No libdrm → app is told fbdev"
    local bindir result
    bindir=$(setup_mock_bindir)
    result=$(backend_for "$bindir" 3 yes "")
    assert "[ '$result' = 'fbdev' ]" "fbdev when the probe reports libdrm missing"
    cleanup_mock_bindir "$bindir"
}

test_no_dri_card_skips_probe() {
    print_test "No DRM card → app auto-detects, no probe"
    local bindir result
    bindir=$(setup_mock_bindir)
    result=$(backend_for "$bindir" 0 no "")
    assert "[ -z '$result' ]" "No backend passed on without a DRM card"
    assert "[ '$(probe_count "$bindir")' = 0 ]" "The probe never ran"
    cleanup_mock_bindir "$bindir"
}

test_env_forced_egl_skips_probe() {
    print_test "HELIX_DISPLAY_BACKEND=egl → app is told egl without probing"
    local bindir result
    bindir=$(setup_mock_bindir)
    result=$(backend_for "$bindir" 1 yes egl)
    assert "[ '$result' = 'egl' ]" "Forced EGL even though the probe would decline"
    assert "[ '$(probe_count "$bindir")' = 0 ]" "The probe never ran"
    cleanup_mock_bindir "$bindir"
}

test_env_forced_drm_skips_probe() {
    print_test "HELIX_DISPLAY_BACKEND=drm → app is told drm without probing"
    local bindir result
    bindir=$(setup_mock_bindir)
    result=$(backend_for "$bindir" 0 yes drm)
    assert "[ '$result' = 'drm' ]" "Forced drm outranks a probe that would succeed"
    cleanup_mock_bindir "$bindir"
}

test_env_forced_fbdev() {
    print_test "HELIX_DISPLAY_BACKEND=fbdev → app is told fbdev"
    local bindir result
    bindir=$(setup_mock_bindir)
    result=$(backend_for "$bindir" 0 yes fbdev)
    assert "[ '$result' = 'fbdev' ]" "Forced fbdev reaches the app"
    cleanup_mock_bindir "$bindir"
}

# Run all tests
main() {
    echo -e "${BOLD}${CYAN}Launcher Binary Selection Test Harness${RESET}"
    echo -e "${CYAN}Project:${RESET} $PROJECT_ROOT"
    echo ""

    test_probe_ok_tells_app_egl
    test_probe_declines_tells_app_drm_not_fbdev
    test_probe_no_rungs_leaves_auto
    test_probe_no_drm_tells_app_fbdev
    test_no_dri_card_skips_probe
    test_env_forced_egl_skips_probe
    test_env_forced_drm_skips_probe
    test_env_forced_fbdev

    # Print summary
    echo ""
    echo -e "${BOLD}${CYAN}Test Summary${RESET}"
    echo -e "${CYAN}────────────────────────────────────────${RESET}"
    echo -e "Total tests: $TESTS_RUN"
    echo -e "${GREEN}Passed: $TESTS_PASSED${RESET}"

    if [ $TESTS_FAILED -gt 0 ]; then
        echo -e "${RED}Failed: $TESTS_FAILED${RESET}"
        echo ""
        echo -e "${RED}${BOLD}Failed tests:${RESET}"
        for test in "${FAILED_TESTS[@]}"; do
            echo -e "  ${RED}✗${RESET} $test"
        done
        exit 1
    else
        echo -e "${GREEN}${BOLD}✓ All tests passed!${RESET}"
        exit 0
    fi
}

main
