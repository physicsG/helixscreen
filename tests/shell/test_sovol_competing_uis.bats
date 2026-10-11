#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests that the Sovol SV06 Ace stock touchscreen UI service `mksclient`
# is detected and disabled by stop_competing_uis() (GitHub #986).
#
# Before the fix, `mksclient` was missing from the COMPETING_UIS list, so the
# installer reported "No competing UIs found" on Sovol SV06 Ace hardware.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

setup() {
    load helpers

    # Temporary install directory for the .disabled_services state file
    export INSTALL_DIR="$BATS_TEST_TMPDIR/opt/helixscreen"
    mkdir -p "$INSTALL_DIR/config"
    export DISABLED_SERVICES_FILE="$INSTALL_DIR/config/.disabled_services"

    # Mock root mirroring a Sovol layout so the SysV glob can be redirected
    # at it (the production loop hardcodes absolute /etc/init.d paths). SUDO
    # stays empty so file ops happen directly under MOCK_ROOT instead of /etc.
    # Also mirror /home (mksclient binary) and /sys/class/gpio (camera light).
    export MOCK_ROOT="$BATS_TEST_TMPDIR/sovol"
    mkdir -p "$MOCK_ROOT/etc/init.d" \
             "$MOCK_ROOT/home/sovol/printer_data/build" \
             "$MOCK_ROOT/sys/class/gpio"

    # Stub functions referenced by stop_competing_uis() but not under test.
    detect_init_system() { INIT_SYSTEM="systemd"; }
    export -f detect_init_system
    INIT_SYSTEM="systemd"
    AD5M_FIRMWARE=""
    K1_FIRMWARE=""
    platform=""
    PREVIOUS_UI_SCRIPT=""
    SERVICE_NAME="helixscreen"

    # Source the production module with absolute /etc/init.d paths redirected
    # to MOCK_ROOT, matching the substitution approach in
    # test_cc1_competing_uis.bats. This exercises the real generic loop rather
    # than a re-implementation.
    local patched="$BATS_TEST_TMPDIR/competing_uis.sh"
    sed -e "s|/etc/init.d/|$MOCK_ROOT/etc/init.d/|g" \
        -e "s|/home/|$MOCK_ROOT/home/|g" \
        -e "s|/sys/class/gpio|$MOCK_ROOT/sys/class/gpio|g" \
        "$WORKTREE_ROOT/scripts/lib/installer/competing_uis.sh" > "$patched"
    unset _HELIX_COMPETING_UIS_SOURCED
    # shellcheck disable=SC1090
    . "$patched"
}

@test "sovol: mksclient is in the COMPETING_UIS list" {
    echo "$COMPETING_UIS" | grep -qw "mksclient"
}

@test "sovol: stops and disables the mksclient systemd service" {
    # systemctl reports mksclient active; log every invocation.
    local systemctl_log="$BATS_TEST_TMPDIR/systemctl.log"
    mock_command_script "systemctl" \
        "echo \"\$@\" >> \"$systemctl_log\"; case \"\$1 \$2\" in \"is-active --quiet\") [ \"\$3\" = mksclient ] && exit 0; exit 1;; esac; exit 0"

    # No competing processes in the test environment.
    kill_process_by_name() { return 1; }
    export -f kill_process_by_name

    run stop_competing_uis
    [ "$status" -eq 0 ]

    # systemctl stop/disable were invoked for mksclient...
    grep -q "stop mksclient" "$systemctl_log"
    grep -q "disable mksclient" "$systemctl_log"
    # ...and the service was recorded for later re-enablement.
    grep -qF "systemd-dropin:mksclient" "$DISABLED_SERVICES_FILE"
}

@test "sovol: disables a SysV mksclient init script (chmod a-x + record)" {
    # Stock Sovol SysV init script (faked content + executable bit).
    local initscript="$MOCK_ROOT/etc/init.d/Smksclient"
    cat > "$initscript" <<'EOF'
#!/bin/sh
echo "stock mksclient $1"
EOF
    chmod +x "$initscript"

    # No systemd service active; just log invocations.
    mock_command_script "systemctl" "exit 1"
    kill_process_by_name() { return 1; }
    export -f kill_process_by_name

    run stop_competing_uis
    [ "$status" -eq 0 ]

    # Init script had its execute bit removed (non-destructive disable)...
    [ ! -x "$initscript" ]
    # ...and was recorded as a sysv-chmod entry for re-enablement.
    grep -qF "sysv-chmod:$initscript" "$DISABLED_SERVICES_FILE"
}

@test "sovol: kills a leftover mksclient process by name" {
    mock_command_script "systemctl" "exit 1"

    # Record which process names kill_process_by_name was asked to kill.
    local kill_log="$BATS_TEST_TMPDIR/kill.log"
    kill_process_by_name() { echo "$1" >> "$kill_log"; return 1; }
    export -f kill_process_by_name

    run stop_competing_uis
    [ "$status" -eq 0 ]
    grep -qw "mksclient" "$kill_log"
}

# --- stop_sovol_competing_uis() handler (#986) ---

# Fake the stock mksclient binary as an executable file under the mocked /home.
write_mksclient() {
    local bin="$MOCK_ROOT/home/sovol/printer_data/build/mksclient"
    cat > "$bin" <<'EOF'
#!/bin/sh
echo "stock mksclient ui"
EOF
    chmod +x "$bin"
    echo "$bin"
}

@test "sovol handler: chmod-a-x's the mksclient binary and records it" {
    local bin
    bin="$(write_mksclient)"
    [ -x "$bin" ]

    kill_process_by_name() { return 1; }
    export -f kill_process_by_name

    found_any=false
    run stop_sovol_competing_uis
    [ "$status" -eq 0 ]

    # Execute bit removed (persistent, reversible disable of the binary)...
    [ ! -x "$bin" ]
    # ...and recorded as a sysv-chmod entry whose path is the binary itself.
    grep -qF "sysv-chmod:$bin" "$DISABLED_SERVICES_FILE"
}

@test "sovol handler: kills the running mksclient process by name" {
    write_mksclient >/dev/null

    local kill_log="$BATS_TEST_TMPDIR/handler_kill.log"
    kill_process_by_name() { echo "$1" >> "$kill_log"; return 1; }
    export -f kill_process_by_name

    found_any=false
    run stop_sovol_competing_uis
    [ "$status" -eq 0 ]
    grep -qw "mksclient" "$kill_log"
}

@test "sovol handler: unexports GPIO 67 when the camera-light line is exported" {
    write_mksclient >/dev/null

    # Simulate mksclient having exported the camera-light GPIO.
    mkdir -p "$MOCK_ROOT/sys/class/gpio/gpio67"
    local unexport="$MOCK_ROOT/sys/class/gpio/unexport"
    : > "$unexport"

    kill_process_by_name() { return 1; }
    export -f kill_process_by_name

    found_any=false
    run stop_sovol_competing_uis
    [ "$status" -eq 0 ]

    # The handler wrote "67" to the unexport node to release the line.
    grep -qx "67" "$unexport"
}

@test "sovol handler: absent mksclient binary is a no-op" {
    # No binary written → handler must not record anything or set found_any.
    rm -f "$MOCK_ROOT/home/sovol/printer_data/build/mksclient"

    kill_process_by_name() {
        echo "SHOULD NOT BE CALLED" >&2
        return 1
    }
    export -f kill_process_by_name

    found_any=false
    run stop_sovol_competing_uis
    [ "$status" -eq 0 ]

    # Nothing recorded for the binary.
    if [ -f "$DISABLED_SERVICES_FILE" ]; then
        refute grep -q "mksclient" "$DISABLED_SERVICES_FILE"
    fi
}

@test "sovol handler: dispatch fires the handler when the binary exists" {
    local bin
    bin="$(write_mksclient)"

    mock_command_script "systemctl" "exit 1"
    kill_process_by_name() { return 1; }
    export -f kill_process_by_name

    run stop_competing_uis
    [ "$status" -eq 0 ]

    # The full dispatch path disabled the binary via the dedicated handler.
    [ ! -x "$bin" ]
    grep -qF "sysv-chmod:$bin" "$DISABLED_SERVICES_FILE"
}
