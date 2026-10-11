#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for stop_wayland_compositors() in competing_uis.sh.
# A Wayland compositor (cage/weston/labwc/sway) holds the DRM/KMS master on
# Armbian/Pi-class boards (e.g. BTT CB1 running KlipperScreen-under-cage),
# blocking HelixScreen's direct DRM output with EACCES until it is stopped.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

setup() {
    load helpers
    # The developer's own desktop session must not read as the installer's.
    unset DISPLAY WAYLAND_DISPLAY XDG_SESSION_TYPE
    log_info() { echo "INFO: $*"; }
    log_warn() { echo "WARN: $*"; }
    export -f log_info log_warn

    # record_disabled_service() shells out to file_sudo; stub it to a no-op so
    # file ops happen directly under the test tmpdir.
    file_sudo() { :; }
    export -f file_sudo

    export SUDO=""
    export INSTALL_DIR="$BATS_TEST_TMPDIR/install"
    mkdir -p "$INSTALL_DIR/config"

    # Mock systemctl: only the service named in MOCK_ACTIVE_SVC is "active".
    # stop/disable are recorded to MOCK_SYSTEMCTL_LOG.
    export MOCK_SYSTEMCTL_LOG="$BATS_TEST_TMPDIR/systemctl.log"
    export MOCK_ACTIVE_SVC=""
    cat > "$BATS_TEST_TMPDIR/systemctl" <<'EOF'
#!/usr/bin/env bash
: > /dev/null
cmd="$1"; shift
case "$cmd" in
  is-active)
    # args: --quiet <svc>
    svc="${@: -1}"
    [ "$svc" = "$MOCK_ACTIVE_SVC" ] && exit 0 || exit 1 ;;
  stop|disable)
    echo "$cmd ${@: -1}" >> "$MOCK_SYSTEMCTL_LOG" ; exit 0 ;;
  show)
    # systemd >= 230 answers `show -p Id --value <name>` with <name>.service
    echo "${@: -1}.service" ; exit 0 ;;
  *) exit 0 ;;
esac
EOF
    chmod +x "$BATS_TEST_TMPDIR/systemctl"
    export PATH="$BATS_TEST_TMPDIR:$PATH"

    # Mock kill_process_by_name (lives in common.sh, not sourced here). Returns 0
    # (killed) only for executables listed in MOCK_RUNNING_PROCS.
    export MOCK_RUNNING_PROCS=""
    kill_process_by_name() {
        local p
        for p in "$@"; do
            case " $MOCK_RUNNING_PROCS " in
                *" $p "*) return 0 ;;
            esac
        done
        return 1
    }
    export -f kill_process_by_name

    unset _HELIX_COMPETING_UIS_SOURCED
    # shellcheck disable=SC1090
    . "$WORKTREE_ROOT/scripts/lib/installer/competing_uis.sh"
}

@test "wayland: stops + disables an active compositor systemd service" {
    export INIT_SYSTEM="systemd"
    export MOCK_ACTIVE_SVC="cage@tty1"
    found_any=false
    run stop_wayland_compositors
    [ "$status" -eq 0 ]
    grep -q "stop cage@tty1" "$MOCK_SYSTEMCTL_LOG"
    grep -q "disable cage@tty1" "$MOCK_SYSTEMCTL_LOG"
}

@test "wayland: records disabled service for re-enablement on uninstall" {
    export INIT_SYSTEM="systemd"
    export MOCK_ACTIVE_SVC="weston"
    found_any=false
    stop_wayland_compositors >/dev/null
    grep -q "^systemd-dropin:weston.service$" "$INSTALL_DIR/config/.disabled_services"
}

@test "wayland: kills a lingering compositor process (no systemd unit)" {
    export INIT_SYSTEM="sysv"
    export MOCK_RUNNING_PROCS="cage"
    found_any=false
    run stop_wayland_compositors
    [ "$status" -eq 0 ]
    [[ "$output" == *"Killed lingering Wayland compositor: cage"* ]]
}

@test "wayland: sets found_any=true when a compositor is stopped" {
    export INIT_SYSTEM="sysv"
    export MOCK_RUNNING_PROCS="sway"
    found_any=false
    stop_wayland_compositors >/dev/null
    [ "$found_any" = true ]
}

@test "wayland: no compositor running is a clean no-op" {
    export INIT_SYSTEM="systemd"
    export MOCK_ACTIVE_SVC=""
    export MOCK_RUNNING_PROCS=""
    found_any=false
    run stop_wayland_compositors
    [ "$status" -eq 0 ]
    [ ! -f "$MOCK_SYSTEMCTL_LOG" ] || [ ! -s "$MOCK_SYSTEMCTL_LOG" ]
    # found_any must remain false (checked in a non-subshell invocation)
    found_any=false
    stop_wayland_compositors >/dev/null
    [ "$found_any" = false ]
}

@test "wayland: inside a graphical session a compositor unit is disabled, not stopped" {
    export INIT_SYSTEM="systemd"
    export MOCK_ACTIVE_SVC="labwc"
    export MOCK_RUNNING_PROCS="labwc"
    mock_proc $$ 4000
    mock_proc 4000 1 "WAYLAND_DISPLAY=wayland-0"
    found_any=false

    run stop_wayland_compositors
    [ "$status" -eq 0 ]

    grep -q "^disable labwc.service$" "$MOCK_SYSTEMCTL_LOG"
    refute_grep "^stop labwc" "$MOCK_SYSTEMCTL_LOG"
    grep -qxF "systemd-dropin:labwc.service" "$INSTALL_DIR/config/.disabled_services"
    contains "Reboot" "$output"
    lacks "Killed lingering" "$output"
}

@test "wayland: inside a graphical session a unit-less compositor is not killed" {
    export MOCK_RUNNING_PROCS="wayfire"
    kill_log="$BATS_TEST_TMPDIR/kill.log"
    kill_process_by_name() { echo "$*" >> "$kill_log"; return 0; }
    export -f kill_process_by_name
    mock_proc $$ 4000
    mock_proc 4000 1 "DISPLAY=:0"
    found_any=false

    run stop_wayland_compositors
    [ "$status" -eq 0 ]

    [ ! -s "$kill_log" ]
}
