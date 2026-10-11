#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests to ensure the monolithic install.sh stays in sync with modular sources.
# Specifically: every function called in install.sh must be DEFINED in install.sh.
# This prevents regressions like the check_klipper_ecosystem bug (#39).

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"
INSTALL_SH="$INSTALL_BUNDLE"
MODULAR_DIR="$WORKTREE_ROOT/scripts/lib/installer"

# Extract function names defined in a shell script (lines matching "funcname() {")
get_defined_functions() {
    grep -oE '^[a-zA-Z_][a-zA-Z0-9_]*\(\)' "$1" | sed 's/()//'
}

# Extract function calls of the form "funcname " or "funcname\n" that look like
# top-level calls (not definitions). This is a rough heuristic.
get_called_functions() {
    # Get all function-like calls, excluding definitions and comments
    grep -oE '[a-zA-Z_][a-zA-Z0-9_]*' "$1" | sort -u
}

# --- Regression test for #39: check_klipper_ecosystem must be defined ---

@test "install.sh defines check_klipper_ecosystem function" {
    grep -q '^check_klipper_ecosystem()' "$INSTALL_SH"
}

@test "install.sh calls check_klipper_ecosystem" {
    grep -q 'check_klipper_ecosystem "\$platform"' "$INSTALL_SH"
}

# --- Every function defined in modular requirements.sh must exist in install.sh ---

@test "all requirements.sh functions are defined in install.sh" {
    local missing=""
    for func in $(get_defined_functions "$MODULAR_DIR/requirements.sh"); do
        if ! grep -q "^${func}()" "$INSTALL_SH"; then
            missing="$missing $func"
        fi
    done
    if [ -n "$missing" ]; then
        echo "Functions defined in requirements.sh but MISSING from install.sh:$missing"
        false
    fi
}

# --- Broader check: every modular function called in install.sh must be defined there ---

@test "all modular lib functions called in main() are defined in install.sh" {
    # Get all functions defined across modular sources
    local modular_funcs=""
    for mod in "$MODULAR_DIR"/*.sh; do
        modular_funcs="$modular_funcs $(get_defined_functions "$mod")"
    done

    # Get all functions defined in install.sh
    local mono_funcs
    mono_funcs=$(get_defined_functions "$INSTALL_SH")

    # For each modular function that is CALLED in install.sh, verify it's also DEFINED there
    local missing=""
    for func in $modular_funcs; do
        # Check if install.sh calls this function (not just in a comment)
        if grep -q "[^#]*$func" "$INSTALL_SH"; then
            # It's referenced — make sure it's defined
            if ! echo "$mono_funcs" | grep -qw "$func"; then
                missing="$missing $func"
            fi
        fi
    done

    if [ -n "$missing" ]; then
        echo "Functions from modular libs called in install.sh but NOT defined:$missing"
        false
    fi
}

# --- Functional: check_klipper_ecosystem returns 0 for pi platform ---

@test "check_klipper_ecosystem returns 0 for pi (no-op)" {
    load helpers
    unset _HELIX_REQUIREMENTS_SOURCED
    . "$MODULAR_DIR/common.sh" 2>/dev/null || true
    . "$MODULAR_DIR/requirements.sh"

    run check_klipper_ecosystem "pi"
    [ "$status" -eq 0 ]
}

@test "check_klipper_ecosystem returns 0 for pi32 (no-op)" {
    load helpers
    unset _HELIX_REQUIREMENTS_SOURCED
    . "$MODULAR_DIR/common.sh" 2>/dev/null || true
    . "$MODULAR_DIR/requirements.sh"

    run check_klipper_ecosystem "pi32"
    [ "$status" -eq 0 ]
}
