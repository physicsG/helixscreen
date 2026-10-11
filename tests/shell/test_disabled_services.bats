#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for disabled service tracking and re-enablement
# Tests record_disabled_service() and reenable_disabled_services()

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

setup() {
    load helpers

    # Set up a temporary install directory
    export INSTALL_DIR="$BATS_TEST_TMPDIR/opt/helixscreen"
    mkdir -p "$INSTALL_DIR/config"
    export DISABLED_SERVICES_FILE="$INSTALL_DIR/config/.disabled_services"

    # Source modules (reset source guards)
    unset _HELIX_COMPETING_UIS_SOURCED
    unset _HELIX_SERVICE_SOURCED
    unset _HELIX_UNINSTALL_SOURCED

    # Stub out functions used by competing_uis.sh but not under test
    kill_process_by_name() { :; }
    export -f kill_process_by_name
    detect_init_system() { INIT_SYSTEM="systemd"; }
    export -f detect_init_system
    INIT_SYSTEM="systemd"
    AD5M_FIRMWARE=""
    PREVIOUS_UI_SCRIPT=""
    SERVICE_NAME="helixscreen"
    HELIX_INIT_SCRIPTS=""
    HELIX_INSTALL_DIRS="$INSTALL_DIR"
    HELIX_PROCESSES=""

    . "$WORKTREE_ROOT/scripts/lib/installer/competing_uis.sh"
    # service.sh before uninstall.sh, the order every production entry
    # point uses: uninstall.sh's rc.common shebang checks call
    # is_rc_common_script from service.sh.
    # shellcheck disable=SC1090
    . "$WORKTREE_ROOT/scripts/lib/installer/service.sh"
    . "$WORKTREE_ROOT/scripts/lib/installer/uninstall.sh"
}

# --- record_disabled_service tests ---

@test "record: creates state file on first call" {
    rm -f "$DISABLED_SERVICES_FILE"
    record_disabled_service "systemd" "KlipperScreen"
    [ -f "$DISABLED_SERVICES_FILE" ]
}

@test "record: appends systemd entry format" {
    record_disabled_service "systemd" "KlipperScreen"
    grep -q "systemd:KlipperScreen" "$DISABLED_SERVICES_FILE"
}

@test "record: appends sysv-chmod entry format" {
    record_disabled_service "sysv-chmod" "/etc/init.d/S40xorg"
    grep -q "sysv-chmod:/etc/init.d/S40xorg" "$DISABLED_SERVICES_FILE"
}

@test "record: deduplicates same entry" {
    record_disabled_service "systemd" "KlipperScreen"
    record_disabled_service "systemd" "KlipperScreen"
    local count
    count=$(grep -c "systemd:KlipperScreen" "$DISABLED_SERVICES_FILE")
    [ "$count" -eq 1 ]
}

@test "record: an entry that is a prefix of a recorded one is still recorded" {
    record_disabled_service "systemd-dropin" "gdm3"
    record_disabled_service "systemd-dropin" "gdm"
    grep -qxF "systemd-dropin:gdm" "$DISABLED_SERVICES_FILE"
}

@test "record: handles multiple different entries" {
    record_disabled_service "systemd" "KlipperScreen"
    record_disabled_service "sysv-chmod" "/etc/init.d/S40xorg"
    record_disabled_service "sysv-chmod" "/etc/init.d/S80klipperscreen"
    [ "$(wc -l < "$DISABLED_SERVICES_FILE" | tr -d ' ')" -eq 3 ]
}

@test "record: state file is inside INSTALL_DIR/config" {
    record_disabled_service "systemd" "test"
    echo "$DISABLED_SERVICES_FILE" | grep -q "$INSTALL_DIR/config/"
}

@test "record: creates config dir if missing" {
    rm -rf "$INSTALL_DIR/config"
    record_disabled_service "systemd" "KlipperScreen"
    [ -d "$INSTALL_DIR/config" ]
    [ -f "$DISABLED_SERVICES_FILE" ]
}

# --- reenable_disabled_services tests ---

@test "reenable: re-enables systemd services" {
    # Record a service
    echo "systemd:TestService" > "$DISABLED_SERVICES_FILE"

    # Mock systemctl to verify "enable" is called
    local enable_log="$BATS_TEST_TMPDIR/systemctl.log"
    mock_command_script "systemctl" "echo \"\$@\" >> \"$enable_log\""

    reenable_disabled_services
    grep -q "enable TestService" "$enable_log"
}

@test "reenable: re-enables sysv-chmod scripts" {
    local test_script="$BATS_TEST_TMPDIR/S40xorg"
    touch "$test_script"
    chmod -x "$test_script"
    echo "sysv-chmod:$test_script" > "$DISABLED_SERVICES_FILE"

    reenable_disabled_services
    [ -x "$test_script" ]
}

@test "reenable: skips empty lines in state file" {
    printf "systemd:TestService\n\nsysv-chmod:/fake\n" > "$DISABLED_SERVICES_FILE"

    local enable_log="$BATS_TEST_TMPDIR/systemctl.log"
    mock_command_script "systemctl" "echo \"\$@\" >> \"$enable_log\""

    reenable_disabled_services
    # Should not crash; systemd entry should be processed
    grep -q "enable TestService" "$enable_log"
}

@test "reenable: skips comment lines in state file" {
    printf "# This is a comment\nsystemd:TestService\n" > "$DISABLED_SERVICES_FILE"

    local enable_log="$BATS_TEST_TMPDIR/systemctl.log"
    mock_command_script "systemctl" "echo \"\$@\" >> \"$enable_log\""

    reenable_disabled_services
    grep -q "enable TestService" "$enable_log"
}

@test "reenable: handles missing state file gracefully" {
    rm -f "$DISABLED_SERVICES_FILE"
    run reenable_disabled_services
    [ "$status" -eq 0 ]
}

@test "reenable: handles nonexistent sysv script" {
    echo "sysv-chmod:/nonexistent/script" > "$DISABLED_SERVICES_FILE"
    run reenable_disabled_services
    [ "$status" -eq 0 ]
}

@test "reenable: processes all entries" {
    local enable_log="$BATS_TEST_TMPDIR/systemctl.log"
    mock_command_script "systemctl" "echo \"\$@\" >> \"$enable_log\""

    local test_script="$BATS_TEST_TMPDIR/S99test"
    touch "$test_script"

    printf "systemd:ServiceA\nsystemd:ServiceB\nsysv-chmod:%s\n" "$test_script" > "$DISABLED_SERVICES_FILE"

    reenable_disabled_services
    grep -q "enable ServiceA" "$enable_log"
    grep -q "enable ServiceB" "$enable_log"
    [ -x "$test_script" ]
}

# --- Integration: record then reenable round-trip ---

@test "integration: record then reenable round-trip" {
    local test_script="$BATS_TEST_TMPDIR/S80klipper"
    touch "$test_script"

    record_disabled_service "sysv-chmod" "$test_script"
    chmod -x "$test_script"

    reenable_disabled_services
    [ -x "$test_script" ]
}

@test "integration: state file survives duplicate record calls" {
    record_disabled_service "systemd" "KlipperScreen"
    record_disabled_service "systemd" "KlipperScreen"
    record_disabled_service "sysv-chmod" "/etc/init.d/S40xorg"
    [ "$(wc -l < "$DISABLED_SERVICES_FILE" | tr -d ' ')" -eq 2 ]
}

# --- Regression: existing uninstall.sh SysV re-enablement untouched ---

@test "regression: uninstall.sh still has chmod +x for S40xorg pattern" {
    grep -q 'S40xorg' "$WORKTREE_ROOT/scripts/lib/installer/uninstall.sh"
}

@test "regression: uninstall.sh still has chmod +x for S80klipperscreen pattern" {
    grep -q 'S80klipperscreen' "$WORKTREE_ROOT/scripts/lib/installer/uninstall.sh"
}
