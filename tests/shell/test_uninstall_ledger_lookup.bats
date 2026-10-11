#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The installer now keeps a copy of the disabled-services ledger in more than
# one place, so reenable_disabled_services() and error_handler() must look
# past ${INSTALL_DIR}/config for a surviving copy after an interrupted
# install or --clean (prestonbrown/helixscreen#1618).

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

setup() {
    load helpers

    export INSTALL_DIR="$BATS_TEST_TMPDIR/opt/helixscreen"
    mkdir -p "$INSTALL_DIR/config"

    export KLIPPER_CONFIG_DIR="$BATS_TEST_TMPDIR/printer_data/config"
    mkdir -p "$KLIPPER_CONFIG_DIR"

    unset _HELIX_COMMON_SOURCED
    unset _HELIX_COMPETING_UIS_SOURCED
    unset _HELIX_UNINSTALL_SOURCED

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
    SUDO=""
    GITHUB_REPO="prestonbrown/helixscreen"
    CLEANUP_TMP=false
    BACKUP_CONFIG=""
    BACKUP_ENV=""

    . "$WORKTREE_ROOT/scripts/lib/installer/common.sh"
    . "$WORKTREE_ROOT/scripts/lib/installer/competing_uis.sh"
    . "$WORKTREE_ROOT/scripts/lib/installer/uninstall.sh"

    # Override the no-op logging from helpers.bash so message text is
    # assertable (bats runs each test in its own subshell, so this is
    # test-scoped and never leaks into another file).
    log_info() { printf '%s\n' "$*"; }
    log_warn() { printf '%s\n' "$*"; }
    log_error() { printf '%s\n' "$*"; }
    log_success() { printf '%s\n' "$*"; }
}

# error_handler() reads $? as the exit code to report, so give it one before
# calling.
_trigger_error_handler() {
    (exit 3)
    error_handler 42
}

# --- Interrupted state A: --clean stranded the ledger at .clean-keep ---

@test "reenable_disabled_services: finds the ledger an interrupted --clean stranded at .clean-keep" {
    local ui_script="$BATS_TEST_TMPDIR/S99start_app"
    touch "$ui_script"
    chmod -x "$ui_script"
    echo "sysv-chmod:$ui_script" > "$KLIPPER_CONFIG_DIR/.disabled_services.clean-keep"

    # Neither the primary path nor the live printer_data copy exists - this
    # is the one surviving copy.
    reenable_disabled_services

    [ "$HELIX_DISABLED_RECORD_FOUND" = "1" ]
    [ -x "$ui_script" ]
}

# --- Interrupted state B: an interrupted first-install swap stranded the
# --- ledger at ${INSTALL_DIR}.old ---

@test "reenable_disabled_services: finds the ledger an interrupted first-install swap stranded at .old" {
    local ui_script="$BATS_TEST_TMPDIR/S99start_app"
    touch "$ui_script"
    chmod -x "$ui_script"
    mkdir -p "${INSTALL_DIR}.old/config"
    echo "sysv-chmod:$ui_script" > "${INSTALL_DIR}.old/config/.disabled_services"

    # extract_release moved $INSTALL_DIR aside before writing the fresh tree;
    # setup_config_symlink never ran, so printer_data/config/helixscreen
    # never existed either.
    reenable_disabled_services

    [ "$HELIX_DISABLED_RECORD_FOUND" = "1" ]
    [ -x "$ui_script" ]
}

# --- Other backup forms release.sh's extract_release can leave INSTALL_BACKUP
# --- pointing at, which this module derives independently of that variable
# --- (see _disabled_services_ledger_candidates) ---

@test "reenable_disabled_services: finds the ledger in a timestamped install backup (NoNewPrivileges fallback)" {
    local ui_script="$BATS_TEST_TMPDIR/S99start_app"
    touch "$ui_script"
    chmod -x "$ui_script"
    mkdir -p "${INSTALL_DIR}.old.1700000000/config"
    echo "sysv-chmod:$ui_script" > "${INSTALL_DIR}.old.1700000000/config/.disabled_services"

    # backup_install_dir_for_update() falls back to this name when a stale
    # .old is root-owned and cannot be removed.
    reenable_disabled_services

    [ "$HELIX_DISABLED_RECORD_FOUND" = "1" ]
    [ -x "$ui_script" ]
}

@test "reenable_disabled_services: the newest timestamped install backup wins over an older one" {
    local old_script="$BATS_TEST_TMPDIR/S_old"
    local new_script="$BATS_TEST_TMPDIR/S_new"
    touch "$old_script" "$new_script"
    chmod -x "$old_script" "$new_script"
    mkdir -p "${INSTALL_DIR}.old.1700000000/config" "${INSTALL_DIR}.old.1800000000/config"
    echo "sysv-chmod:$old_script" > "${INSTALL_DIR}.old.1700000000/config/.disabled_services"
    echo "sysv-chmod:$new_script" > "${INSTALL_DIR}.old.1800000000/config/.disabled_services"

    reenable_disabled_services

    [ -x "$new_script" ]
    [ ! -x "$old_script" ]
}

@test "reenable_disabled_services: finds the ledger under an offsite rollback mount" {
    local ui_script="$BATS_TEST_TMPDIR/S99start_app"
    touch "$ui_script"
    chmod -x "$ui_script"
    local roomy="$BATS_TEST_TMPDIR/mnt/UDISK"
    mkdir -p "$roomy/helixscreen-rollback/helixscreen/config"
    echo "sysv-chmod:$ui_script" > "$roomy/helixscreen-rollback/helixscreen/config/.disabled_services"
    HELIX_ROLLBACK_CANDIDATES="$roomy"

    # extract_release relocates the old install here when the install
    # filesystem is too tight to hold the old and new tree at once.
    reenable_disabled_services

    [ "$HELIX_DISABLED_RECORD_FOUND" = "1" ]
    [ -x "$ui_script" ]
}

# --- Precedence: a live ledger must win over a stranded one ---

@test "reenable_disabled_services: the live per-install ledger wins over a stranded clean-keep carry" {
    local live_script="$BATS_TEST_TMPDIR/S_live"
    local stale_script="$BATS_TEST_TMPDIR/S_stale"
    touch "$live_script" "$stale_script"
    chmod -x "$live_script" "$stale_script"

    echo "sysv-chmod:$live_script" > "$INSTALL_DIR/config/.disabled_services"
    echo "sysv-chmod:$stale_script" > "$KLIPPER_CONFIG_DIR/.disabled_services.clean-keep"

    reenable_disabled_services

    [ -x "$live_script" ]
    [ ! -x "$stale_script" ]
}

@test "reenable_disabled_services: the printer_data copy wins over a stranded clean-keep carry" {
    mkdir -p "$KLIPPER_CONFIG_DIR/helixscreen"
    local live_script="$BATS_TEST_TMPDIR/S_pd"
    local stale_script="$BATS_TEST_TMPDIR/S_stale2"
    touch "$live_script" "$stale_script"
    chmod -x "$live_script" "$stale_script"

    echo "sysv-chmod:$live_script" > "$KLIPPER_CONFIG_DIR/helixscreen/.disabled_services"
    echo "sysv-chmod:$stale_script" > "$KLIPPER_CONFIG_DIR/.disabled_services.clean-keep"

    reenable_disabled_services

    [ -x "$live_script" ]
    [ ! -x "$stale_script" ]
}

# --- error_handler: must not claim original state when it did not restore ---

@test "error_handler: nothing to restore still claims original state" {
    run _trigger_error_handler

    contains "Your system should be in its original state." "$output"
}

@test "error_handler: recovers a ledger stranded in .old and keeps the original-state claim" {
    mkdir -p "${INSTALL_DIR}.old/config"
    echo "sysv-chmod:/etc/init.d/S99start_app" > "${INSTALL_DIR}.old/config/.disabled_services"

    run _trigger_error_handler

    [ -f "${INSTALL_DIR}/config/.disabled_services" ] || fail "ledger not copied forward"
    grep -q "S99start_app" "${INSTALL_DIR}/config/.disabled_services" || fail "copied ledger missing its entry"
    contains "Your system should be in its original state." "$output"
    lacks "could not be recorded for recovery" "$output"
}

@test "error_handler: recovers a ledger from a timestamped install backup, independent of INSTALL_BACKUP" {
    local backup_dir="${INSTALL_DIR}.old.1700000000"
    mkdir -p "$backup_dir/config"
    echo "sysv-chmod:/etc/init.d/S99start_app" > "$backup_dir/config/.disabled_services"
    # The ledger recovery derives this location on its own (see
    # _disabled_services_ledger_candidates); INSTALL_BACKUP set to match is
    # only for realism against a real interrupted-update failure.
    INSTALL_BACKUP="$backup_dir"

    run _trigger_error_handler

    [ -f "${INSTALL_DIR}/config/.disabled_services" ] || fail "ledger not copied forward"
    grep -q "S99start_app" "${INSTALL_DIR}/config/.disabled_services" || fail "copied ledger missing its entry"
    contains "Your system should be in its original state." "$output"
}

@test "error_handler: recovers a ledger from an offsite rollback backup, independent of INSTALL_BACKUP" {
    local roomy="$BATS_TEST_TMPDIR/mnt/UDISK"
    mkdir -p "$roomy/helixscreen-rollback/helixscreen/config"
    echo "sysv-chmod:/etc/init.d/S99start_app" > "$roomy/helixscreen-rollback/helixscreen/config/.disabled_services"
    HELIX_ROLLBACK_CANDIDATES="$roomy"
    INSTALL_BACKUP="$roomy/helixscreen-rollback/helixscreen"

    run _trigger_error_handler

    [ -f "${INSTALL_DIR}/config/.disabled_services" ] || fail "ledger not copied forward"
    grep -q "S99start_app" "${INSTALL_DIR}/config/.disabled_services" || fail "copied ledger missing its entry"
    contains "Your system should be in its original state." "$output"
}

@test "error_handler: a ledger it cannot copy forward drops the original-state claim" {
    mkdir -p "${INSTALL_DIR}.old/config"
    echo "sysv-chmod:/etc/init.d/S99start_app" > "${INSTALL_DIR}.old/config/.disabled_services"

    # Force the restore copy to fail without depending on real filesystem
    # permissions (this suite may run as root, which ignores them).
    cp() { return 1; }

    run _trigger_error_handler

    [ ! -f "${INSTALL_DIR}/config/.disabled_services" ] || fail "ledger should not have been copied forward"
    contains "could not be recorded for recovery" "$output"
    lacks "Your system should be in its original state." "$output"
}
