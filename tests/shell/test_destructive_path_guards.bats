#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Guards on the two user-settable paths the installer destroys:
#
#   TMP_DIR      rm -rf "$TMP_DIR"                        (common.sh)
#   INSTALL_DIR  mv aside + rm -rf + config/ sibling sweep (release.sh, uninstall.sh)
#
# and on --clean's confirmation, which must not treat a non-TTY stdin as
# consent (the documented invocation is `curl … | sh -s -- --clean`, whose
# stdin is the pipe carrying the script).

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

setup() {
    load helpers
    # _safe_remove_tmp_dir's mountpoint probe uses `stat -c '%d'`.
    install_gnu_stat_shim

    unset _HELIX_COMMON_SOURCED _HELIX_HOST_PROFILE_SOURCED _HELIX_PLATFORM_SOURCED
    export SUDO=""
    export TMP_DIR=""
    export INSTALL_DIR=""

    # common.sh owns the guards; platform.sh is their only caller. Production
    # always has both (bundle-installer.sh emits common.sh first), so tests
    # must load both or they exercise a shape that never ships.
    # shellcheck disable=SC1090
    . "$WORKTREE_ROOT/scripts/lib/installer/common.sh"
    . "$WORKTREE_ROOT/scripts/lib/installer/host_profile.sh"
    # shellcheck disable=SC1090
    . "$WORKTREE_ROOT/scripts/lib/installer/platform.sh"

    # Capture log output so refusal messages are assertable. Must come AFTER
    # common.sh, which defines the real (stderr + ANSI) implementations.
    log_info()    { echo "INFO: $*"; }
    log_warn()    { echo "WARN: $*"; }
    log_error()   { echo "ERROR: $*"; }
    log_success() { echo "OK: $*"; }
    export -f log_info log_warn log_error log_success
}

# ===========================================================================
# validate_tmp_dir
# ===========================================================================

@test "validate_tmp_dir: accepts the installer's own scratch names" {
    for d in /tmp/helixscreen-install \
             /var/tmp/helixscreen-install \
             /home/pi/.helixscreen-install \
             /user-resource/helixscreen-install \
             /opt/helixscreen-install/; do
        run validate_tmp_dir "$d"
        [ "$status" -eq 0 ] || fail "rejected legitimate TMP_DIR: $d ($output)"
    done
}

@test "validate_tmp_dir: accepts the in-app updater's staging dir handoff" {
    # update_checker.cpp STAGING_NAME — install.sh is exec'd with this TMP_DIR.
    run validate_tmp_dir "/home/pi/.helix-update-staging"
    [ "$status" -eq 0 ]
}

@test "validate_tmp_dir: refuses a mount root (the /mnt/UDISK wipe)" {
    run validate_tmp_dir "/mnt/UDISK"
    [ "$status" -ne 0 ]
    [[ "$output" == *"Refusing to use TMP_DIR='/mnt/UDISK'"* ]]
}

@test "validate_tmp_dir: refuses a home directory" {
    run validate_tmp_dir "/home/pi"
    [ "$status" -ne 0 ]
    [[ "$output" == *"helixscreen-install"* ]]
}

@test "validate_tmp_dir: refuses the filesystem root, relative paths, and traversal" {
    for d in / /data /usr/data relative/helixscreen-install /a/helixscreen-install/../..; do
        run validate_tmp_dir "$d"
        [ "$status" -ne 0 ] || fail "accepted unsafe TMP_DIR: $d"
    done
}

# ===========================================================================
# validate_install_dir
# ===========================================================================

@test "validate_install_dir: accepts every auto-detected platform path" {
    for d in /opt/helixscreen \
             /srv/helixscreen \
             /usr/data/helixscreen \
             /userdata/helixscreen \
             /user-resource/helixscreen \
             /root/printer_software/helixscreen \
             /home/biqu/helixscreen; do
        run validate_install_dir "$d"
        [ "$status" -eq 0 ] || fail "rejected legitimate INSTALL_DIR: $d ($output)"
    done
}

@test "validate_install_dir: refuses a bare data directory" {
    for d in /home/pi /mnt/UDISK /usr/data /; do
        run validate_install_dir "$d"
        [ "$status" -ne 0 ] || fail "accepted unsafe INSTALL_DIR: $d"
    done
}

@test "validate_install_dir: refusal names the offending value and the rule" {
    run validate_install_dir "/home/pi"
    [ "$status" -ne 0 ]
    contains "Refusing to use INSTALL_DIR='/home/pi'" "$output"
    [[ "$output" == *"must contain 'helixscreen'"* ]]
}

# ===========================================================================
# _safe_remove_tmp_dir — last line of defence on the actual rm -rf
# ===========================================================================

@test "_safe_remove_tmp_dir: removes a properly named scratch dir" {
    local d="$BATS_TEST_TMPDIR/helixscreen-install"
    mkdir -p "$d/extract"
    touch "$d/extract/payload"

    _safe_remove_tmp_dir "$d"
    [ ! -d "$d" ]
}

@test "_safe_remove_tmp_dir: refuses to rm -rf a directory that is not ours" {
    local d="$BATS_TEST_TMPDIR/printer_data"
    mkdir -p "$d/config"
    echo "user gcode macros" > "$d/config/printer.cfg"

    run _safe_remove_tmp_dir "$d"
    [ "$status" -eq 0 ]
    [ -f "$d/config/printer.cfg" ]
    [[ "$output" == *"Refusing to rm -rf"* ]]
}

# ===========================================================================
# Wiring: the guards actually gate the code paths that honour the overrides
# ===========================================================================

@test "detect_tmp_dir: aborts on an unsafe user override instead of using it" {
    export TMP_DIR="/home/pi"
    run detect_tmp_dir
    [ "$status" -ne 0 ]
    [[ "$output" == *"Refusing to use TMP_DIR"* ]]
}

@test "detect_tmp_dir: still honours a safe user override verbatim" {
    export TMP_DIR="/home/pi/helixscreen-install"
    detect_tmp_dir
    [ "$TMP_DIR" = "/home/pi/helixscreen-install" ]
}

@test "detect_pi_install_dir: aborts on an unsafe user override" {
    _USER_INSTALL_DIR="/home/pi"
    KLIPPER_HOME="$BATS_TEST_TMPDIR/home/testuser"
    mkdir -p "$KLIPPER_HOME/klipper"

    run detect_pi_install_dir
    [ "$status" -ne 0 ]
    [[ "$output" == *"Refusing to use INSTALL_DIR"* ]]
}

@test "detect_pi_install_dir: still honours a safe user override" {
    _USER_INSTALL_DIR="/custom/path/helixscreen"
    KLIPPER_HOME="$BATS_TEST_TMPDIR/home/testuser"
    mkdir -p "$KLIPPER_HOME/klipper"

    detect_pi_install_dir
    [ "$INSTALL_DIR" = "/custom/path/helixscreen" ]
}

# ===========================================================================
# --clean confirmation (uninstall.sh)
# ===========================================================================

load_uninstall_module() {
    unset _HELIX_UNINSTALL_SOURCED
    # shellcheck disable=SC1090
    . "$WORKTREE_ROOT/scripts/lib/installer/uninstall.sh"
}

@test "confirm_clean_install: refuses when stdin is a pipe and --yes was not given" {
    load_uninstall_module
    ASSUME_YES=false

    # bats gives the test body a non-TTY stdin, which is exactly the
    # `curl … | sh -s -- --clean` shape.
    run confirm_clean_install
    [ "$status" -eq 1 ]
    contains "Refusing to run --clean without confirmation" "$output"
    [[ "$output" == *"--yes"* ]]
}

@test "confirm_clean_install: proceeds when --yes was given" {
    load_uninstall_module
    ASSUME_YES=true

    run confirm_clean_install
    [ "$status" -eq 0 ]
    [[ "$output" == *"proceeding without confirmation"* ]]
}

@test "installer parses --yes and hands it to the clean confirmation" {
    # main.sh's parser and uninstall.sh's gate must agree on the variable name;
    # a rename on either side silently restores the skip-the-prompt behavior.
    grep -q 'ASSUME_YES=true' "$WORKTREE_ROOT/scripts/lib/installer/main.sh"
    grep -q -- '--yes|-y|--force' "$WORKTREE_ROOT/scripts/lib/installer/main.sh"
    grep -q 'ASSUME_YES:-false' "$WORKTREE_ROOT/scripts/lib/installer/uninstall.sh"
}

# ===========================================================================
# uninstall's own-root sweep entry (helix_install_dirs_for_run)
#
# uninstall rm -rf's every entry of the list; INSTALL_DIR joins it only when
# every guard in _uninstall_own_root_ok holds.
# ===========================================================================

_load_uninstall() {
    # shellcheck disable=SC1090
    . "$WORKTREE_ROOT/scripts/lib/installer/uninstall.sh"
    export HELIX_INSTALL_DIRS="/opt/helixscreen"
    unset HOST_MOD_ROOT HOST_MOD_CHROOT
}

# A directory holding our binary, the shape every accepted root has.
_seed_root() {
    mkdir -p "$1/bin"
    : > "$1/bin/helix-screen"
    chmod +x "$1/bin/helix-screen"
}

@test "uninstall sweep: a real home install root joins the list" {
    _load_uninstall
    local root="$BATS_TEST_TMPDIR/home/pi/helixscreen"
    _seed_root "$root"
    INSTALL_DIR="$root"
    run helix_install_dirs_for_run
    [ "$output" = "/opt/helixscreen $root" ]
}

@test "uninstall sweep: INSTALL_DIR=\$HOME is left alone" {
    _load_uninstall
    local home="$BATS_TEST_TMPDIR/home/helixscreen"
    _seed_root "$home"
    HOME="$home" INSTALL_DIR="$home"
    run helix_install_dirs_for_run
    [ "$output" = "/opt/helixscreen" ]
}

@test "uninstall sweep: INSTALL_DIR=\$KLIPPER_HOME is left alone" {
    _load_uninstall
    local khome="$BATS_TEST_TMPDIR/srv/helixscreen"
    _seed_root "$khome"
    KLIPPER_HOME="$khome" INSTALL_DIR="$khome"
    run helix_install_dirs_for_run
    [ "$output" = "/opt/helixscreen" ]
}

@test "uninstall sweep: INSTALL_DIR=/ is left alone" {
    _load_uninstall
    INSTALL_DIR="/"
    run helix_install_dirs_for_run
    [ "$output" = "/opt/helixscreen" ]
}

@test "uninstall sweep: a symlinked install root is left alone" {
    _load_uninstall
    local target="$BATS_TEST_TMPDIR/data/helixscreen"
    _seed_root "$target"
    mkdir -p "$BATS_TEST_TMPDIR/home/pi"
    ln -s "$target" "$BATS_TEST_TMPDIR/home/pi/helixscreen"
    INSTALL_DIR="$BATS_TEST_TMPDIR/home/pi/helixscreen"
    run helix_install_dirs_for_run
    [ "$output" = "/opt/helixscreen" ]
    [ -x "$target/bin/helix-screen" ]
}

@test "uninstall sweep: a root reached through a symlinked parent is left alone" {
    _load_uninstall
    local target="$BATS_TEST_TMPDIR/data/pi"
    _seed_root "$target/helixscreen"
    mkdir -p "$BATS_TEST_TMPDIR/home"
    ln -s "$target" "$BATS_TEST_TMPDIR/home/pi"
    INSTALL_DIR="$BATS_TEST_TMPDIR/home/pi/helixscreen"
    run helix_install_dirs_for_run
    [ "$output" = "/opt/helixscreen" ]
}

@test "uninstall sweep: trailing slashes do not change the answer" {
    _load_uninstall
    local root="$BATS_TEST_TMPDIR/home/pi/helixscreen"
    _seed_root "$root"
    INSTALL_DIR="$root//"
    run helix_install_dirs_for_run
    [ "$output" = "/opt/helixscreen $root" ]
}

@test "uninstall sweep: a root whose path holds a space is left alone" {
    # The sweeps iterate the list unquoted: "/mnt/usb drive/helixscreen" would
    # split, and rm -rf would reach "/mnt/usb".
    _load_uninstall
    local root="$BATS_TEST_TMPDIR/mnt/usb drive/helixscreen"
    _seed_root "$root"
    INSTALL_DIR="$root"
    run helix_install_dirs_for_run
    [ "$output" = "/opt/helixscreen" ]
}

@test "uninstall sweep: a root whose path holds a glob character is left alone" {
    _load_uninstall
    local root="$BATS_TEST_TMPDIR/mnt/usb*/helixscreen"
    _seed_root "$root"
    INSTALL_DIR="$root"
    run helix_install_dirs_for_run
    [ "$output" = "/opt/helixscreen" ]
}

@test "uninstall sweep: a root the fixed list already reaches through a symlink is not repeated" {
    _load_uninstall
    local root="$BATS_TEST_TMPDIR/home/pi/helixscreen"
    _seed_root "$root"
    mkdir -p "$BATS_TEST_TMPDIR/opt"
    ln -s "$root" "$BATS_TEST_TMPDIR/opt/helixscreen"
    HELIX_INSTALL_DIRS="$BATS_TEST_TMPDIR/opt/helixscreen"
    INSTALL_DIR="$root"
    run helix_install_dirs_for_run
    [ "$output" = "$BATS_TEST_TMPDIR/opt/helixscreen" ]
}

@test "uninstall sweep: a root without our binary is left alone" {
    _load_uninstall
    INSTALL_DIR="$BATS_TEST_TMPDIR/home/pi/helixscreen"
    mkdir -p "$INSTALL_DIR/bin"
    run helix_install_dirs_for_run
    [ "$output" = "/opt/helixscreen" ]
}

@test "uninstall sweep: a root not named exactly helixscreen is left alone" {
    _load_uninstall
    INSTALL_DIR="$BATS_TEST_TMPDIR/home/pi/helixscreen-old"
    _seed_root "$INSTALL_DIR"
    run helix_install_dirs_for_run
    [ "$output" = "/opt/helixscreen" ]
}
