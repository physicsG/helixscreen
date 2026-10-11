#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Which systemd units the competing-UI sweep acts on.
#
# A screen UI competes for the display in two states, not one: running now, or
# enabled so it takes the display back at the next boot - and the installer asks
# for a reboot at the end. On a Pi/Voron host KlipperScreen is a plain systemd
# unit, so both states are what _unit_is_competing() has to answer for.
#
# The counterweight is over-reach. `systemctl is-enabled` exits 0 for `static`,
# `indirect` and `alias` as well, and none of those names a unit `disable` can
# turn off by itself, so acting on one records a promise uninstall's
# `systemctl enable` cannot keep.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

setup() {
    load helpers

    export INSTALL_DIR="$BATS_TEST_TMPDIR/opt/helixscreen"
    mkdir -p "$INSTALL_DIR/config"
    export DISABLED_SERVICES_FILE="$INSTALL_DIR/config/.disabled_services"
    export SYSTEMCTL_LOG="$BATS_TEST_TMPDIR/systemctl.log"

    # A fake root for every absolute path the sweep reads, so a bare host scan
    # cannot reach the developer machine's own init scripts or units.
    export MOCK_ROOT="$BATS_TEST_TMPDIR/host"
    mkdir -p "$MOCK_ROOT/etc/init.d" \
             "$MOCK_ROOT/etc/systemd/system" \
             "$MOCK_ROOT/lib/systemd/system" \
             "$MOCK_ROOT/home" \
             "$MOCK_ROOT/opt/config/mod/.root"

    local patched="$BATS_TEST_TMPDIR/competing_uis.sh"
    sed -e "s|/etc/init.d/|$MOCK_ROOT/etc/init.d/|g" \
        -e "s|/etc/systemd/system|$MOCK_ROOT/etc/systemd/system|g" \
        -e "s|/lib/systemd/system|$MOCK_ROOT/lib/systemd/system|g" \
        -e "s|/opt/config/mod/.root/|$MOCK_ROOT/opt/config/mod/.root/|g" \
        -e "s|/opt/PROGRAM/|$MOCK_ROOT/opt/PROGRAM/|g" \
        -e "s|/home/|$MOCK_ROOT/home/|g" \
        "$WORKTREE_ROOT/scripts/lib/installer/competing_uis.sh" > "$patched"
    unset _HELIX_COMPETING_UIS_SOURCED
    # shellcheck disable=SC1090
    . "$patched"

    unset _HELIX_UNINSTALL_SOURCED
    . "$WORKTREE_ROOT/scripts/lib/installer/uninstall.sh"

    _is_self_update() { return 1; }
    export -f _is_self_update
    kill_process_by_name() { return 1; }
    export -f kill_process_by_name
    # The sweep waits on a UI it acted on; nothing here is real.
    sleep() { :; }
    export -f sleep

    INIT_SYSTEM="systemd"
    MOD_FLAVOR=""
    AD5M_FIRMWARE=""
    K1_FIRMWARE=""
    HOST_OWNS_COMPETING_UIS=0
    platform="pi"
    # The developer's own desktop session must not leak into the DM tests.
    unset DISPLAY WAYLAND_DISPLAY XDG_SESSION_TYPE
    PREVIOUS_UI_SCRIPT=""
    SERVICE_NAME="helixscreen"
    SUDO=""
}

# Mock systemctl reporting one unit in one state and every other unit absent.
# Logs each invocation so a test can assert on stop/disable/enable.
# `show -p Id --value <name>` answers `<name>.service`, as systemd >= 230 does
# for any name; pass "noid" to model an older systemd that prints nothing.
# Args: $1 = unit name, $2 = is-active exit (0 running), $3 = is-enabled word,
#       $4 = "noid" (optional)
mock_unit_state() {
    local unit="$1" active_exit="$2" enabled_word="$3"
    local show_body='for a; do last=$a; done; echo "$last.service"'
    [ "${4:-}" = "noid" ] && show_body=':'
    local enabled_exit=1
    # systemd exits 0 for `static`, `indirect` and `alias` as well as the enabled
    # words, which is the trap the word match exists to avoid.
    case "$enabled_word" in
        enabled|enabled-runtime|static|indirect|alias) enabled_exit=0 ;;
    esac

    mock_command_script "systemctl" "
echo \"\$@\" >> '$SYSTEMCTL_LOG'
case \"\$1\" in
    is-active)
        case \"\$*\" in *' $unit'*|*'$unit ') exit $active_exit ;; esac
        exit 3
        ;;
    is-enabled)
        case \"\$*\" in *'$unit'*) echo '$enabled_word'; exit $enabled_exit ;; esac
        echo 'not-found'; exit 4
        ;;
    show)
        $show_body
        ;;
esac
exit 0
"
}

# --- _unit_is_competing(): the decision on its own ---

@test "gate: a running unit competes" {
    mock_unit_state "KlipperScreen" 0 "enabled"
    run _unit_is_competing "KlipperScreen"
    [ "$status" -eq 0 ]
}

@test "gate: an enabled but stopped unit competes" {
    mock_unit_state "KlipperScreen" 3 "enabled"
    run _unit_is_competing "KlipperScreen"
    [ "$status" -eq 0 ]
}

@test "gate: an enabled-runtime unit competes" {
    mock_unit_state "KlipperScreen" 3 "enabled-runtime"
    run _unit_is_competing "KlipperScreen"
    [ "$status" -eq 0 ]
}

@test "gate: a stopped, disabled unit does not compete" {
    mock_unit_state "KlipperScreen" 3 "disabled"
    run _unit_is_competing "KlipperScreen"
    [ "$status" -ne 0 ]
}

@test "gate: a stopped static unit does not compete despite is-enabled exiting 0" {
    # `static` has no [Install] section, so `systemctl disable` cannot change it
    # and `systemctl enable` cannot restore it. Acting on exit status alone
    # would record a reversal that uninstall silently fails to perform.
    mock_unit_state "KlipperScreen" 3 "static"
    run _unit_is_competing "KlipperScreen"
    [ "$status" -ne 0 ]
}

@test "gate: a stopped indirect unit does not compete despite is-enabled exiting 0" {
    mock_unit_state "KlipperScreen" 3 "indirect"
    run _unit_is_competing "KlipperScreen"
    [ "$status" -ne 0 ]
}

@test "gate: a stopped alias does not compete despite is-enabled exiting 0" {
    mock_unit_state "KlipperScreen" 3 "alias"
    run _unit_is_competing "KlipperScreen"
    [ "$status" -ne 0 ]
}

@test "gate: a masked unit does not compete" {
    mock_unit_state "KlipperScreen" 3 "masked"
    run _unit_is_competing "KlipperScreen"
    [ "$status" -ne 0 ]
}

@test "gate: an absent unit does not compete" {
    mock_unit_state "SomethingElse" 3 "enabled"
    run _unit_is_competing "KlipperScreen"
    [ "$status" -ne 0 ]
}

# --- stop_competing_uis(): the Pi/Voron KlipperScreen unit ---

@test "pi: a running KlipperScreen is stopped, disabled and recorded" {
    mock_unit_state "KlipperScreen" 0 "enabled"

    run stop_competing_uis
    [ "$status" -eq 0 ]

    grep -q "stop KlipperScreen" "$SYSTEMCTL_LOG"
    grep -q "disable KlipperScreen" "$SYSTEMCTL_LOG"
    grep -qxF "systemd-dropin:KlipperScreen.service" "$DISABLED_SERVICES_FILE"
}

@test "pi: an enabled but stopped KlipperScreen is disabled and recorded" {
    # The reboot case: nothing to stop today, but it owns the display tomorrow.
    mock_unit_state "KlipperScreen" 3 "enabled"

    run stop_competing_uis
    [ "$status" -eq 0 ]

    grep -q "disable KlipperScreen" "$SYSTEMCTL_LOG"
    grep -qxF "systemd-dropin:KlipperScreen.service" "$DISABLED_SERVICES_FILE"
}

@test "pi: a taken-down unit gets a drop-in that keeps it down while HelixScreen is installed" {
    # A KIAUH KlipperScreen unit lives in /etc/systemd/system, where mask is
    # refused; the drop-in survives a re-enable and a rewrite of the unit file.
    mock_unit_state "KlipperScreen" 0 "enabled"

    run stop_competing_uis
    [ "$status" -eq 0 ]

    local dropin
    dropin=$(competing_ui_dropin KlipperScreen.service)
    grep -qxF "[Unit]" "$dropin"
    grep -qxF "ConditionPathExists=!$INSTALL_DIR/bin/helix-screen" "$dropin"
    grep -q "daemon-reload" "$SYSTEMCTL_LOG"
    refute_grep "mask" "$SYSTEMCTL_LOG"
}

@test "pi: a systemd that prints no Id still gets a .service drop-in" {
    # systemd ignores a drop-in dir named after a bare unit name.
    mock_unit_state "KlipperScreen" 0 "enabled" noid

    run stop_competing_uis
    [ "$status" -eq 0 ]

    [ -f "$HELIX_SYSTEMD_UNIT_DIR/KlipperScreen.service.d/helixscreen-competing.conf" ]
    grep -qxF "systemd-dropin:KlipperScreen.service" "$DISABLED_SERVICES_FILE"
}

@test "pi: a drop-in that cannot be written is recorded as a plain disable" {
    # A read-only /etc: the unit is only disabled, and the record must not
    # claim the durable part. A path under a regular file fails even for root.
    mock_unit_state "KlipperScreen" 0 "enabled"
    : > "$BATS_TEST_TMPDIR/not-a-dir"
    HELIX_SYSTEMD_UNIT_DIR="$BATS_TEST_TMPDIR/not-a-dir/system"

    run stop_competing_uis
    [ "$status" -eq 0 ]

    grep -q "disable KlipperScreen" "$SYSTEMCTL_LOG"
    grep -qxF "systemd:KlipperScreen.service" "$DISABLED_SERVICES_FILE"
    refute_grep "systemd-dropin" "$DISABLED_SERVICES_FILE"
}

@test "pi: a unit name is resolved to its Id and taken down once" {
    # gdm3 is an alias of gdm.service on Debian; both names must land on one
    # record and one drop-in, on the real unit.
    platform="k2"
    mock_command_script "systemctl" "
echo \"\$@\" >> '$SYSTEMCTL_LOG'
case \"\$1\" in
    show) case \"\$*\" in *gdm*) echo gdm.service ;; *) echo \"\${5}.service\" ;; esac; exit 0 ;;
    is-active) exit 3 ;;
    is-enabled) case \"\$*\" in *gdm*) echo enabled; exit 0 ;; esac; echo not-found; exit 4 ;;
esac
exit 0
"

    run stop_competing_uis
    [ "$status" -eq 0 ]

    [ "$(grep -c "^disable " "$SYSTEMCTL_LOG")" -eq 1 ]
    grep -q "^disable gdm.service" "$SYSTEMCTL_LOG"
    grep -qxF "systemd-dropin:gdm.service" "$DISABLED_SERVICES_FILE"
    [ -f "$(competing_ui_dropin gdm.service)" ]
}

@test "pi: a KlipperScreen the user already disabled is left alone" {
    mock_unit_state "KlipperScreen" 3 "disabled"

    run stop_competing_uis
    [ "$status" -eq 0 ]

    refute_grep "disable KlipperScreen" "$SYSTEMCTL_LOG"
    [ ! -f "$DISABLED_SERVICES_FILE" ] || refute_grep "KlipperScreen" "$DISABLED_SERVICES_FILE"
}

@test "pi: a stopped static unit is left alone" {
    mock_unit_state "KlipperScreen" 3 "static"

    run stop_competing_uis
    [ "$status" -eq 0 ]

    refute_grep "disable KlipperScreen" "$SYSTEMCTL_LOG"
    [ ! -f "$DISABLED_SERVICES_FILE" ] || refute_grep "KlipperScreen" "$DISABLED_SERVICES_FILE"
}

@test "pi: the sweep never touches the helixscreen unit" {
    mock_unit_state "KlipperScreen" 0 "enabled"

    run stop_competing_uis
    [ "$status" -eq 0 ]

    refute_grep "helixscreen" "$SYSTEMCTL_LOG"
}

# --- stop_wayland_compositors(): the same gate on the DRM master holder ---

@test "compositor: an enabled but stopped cage@tty1 is disabled and recorded" {
    # KlipperScreen under cage: the compositor owns /dev/dri/card0, so a cage
    # unit left enabled takes the DRM master back at the next boot.
    mock_unit_state "cage@tty1" 3 "enabled"

    found_any=false
    run stop_wayland_compositors
    [ "$status" -eq 0 ]

    grep -q "disable cage@tty1" "$SYSTEMCTL_LOG"
    grep -qxF "systemd-dropin:cage@tty1.service" "$DISABLED_SERVICES_FILE"
}

@test "compositor: a stopped, disabled weston is left alone" {
    mock_unit_state "weston" 3 "disabled"

    found_any=false
    run stop_wayland_compositors
    [ "$status" -eq 0 ]

    refute_grep "disable weston" "$SYSTEMCTL_LOG"
    [ ! -f "$DISABLED_SERVICES_FILE" ] || refute_grep "weston" "$DISABLED_SERVICES_FILE"
}

# --- display managers (#1693): a DM starts a session that takes the display ---
# Only on hosts whose firmware ships a screen of its own: a desktop Pi keeps
# its desktop.

@test "display manager: an enabled, failed lightdm on a vendor printer is taken down" {
    # QIDI Q2 firmware 1.1.1: lightdm enabled under graphical.target, failing.
    platform="k2"
    mock_unit_state "lightdm" 3 "enabled"

    run stop_competing_uis
    [ "$status" -eq 0 ]

    grep -q "stop lightdm" "$SYSTEMCTL_LOG"
    grep -q "disable lightdm" "$SYSTEMCTL_LOG"
    grep -q "reset-failed lightdm" "$SYSTEMCTL_LOG"
    grep -qxF "systemd-dropin:lightdm.service" "$DISABLED_SERVICES_FILE"
    [ -f "$(competing_ui_dropin lightdm.service)" ]
    # The default target is the user's: keeping the DM down is enough.
    refute_grep "set-default" "$SYSTEMCTL_LOG"
}

@test "display manager: a generic pi keeps its desktop" {
    platform="pi"
    mock_unit_state "lightdm" 0 "enabled"

    run stop_competing_uis
    [ "$status" -eq 0 ]

    refute_grep "lightdm" "$SYSTEMCTL_LOG"
    [ ! -f "$DISABLED_SERVICES_FILE" ] || refute_grep "lightdm" "$DISABLED_SERVICES_FILE"
}

@test "display manager: inside a graphical session it is disabled but not stopped" {
    # Stopping the DM would take down the terminal running this installer.
    platform="k2"
    mock_unit_state "lightdm" 0 "enabled"
    DISPLAY=":0"

    run stop_competing_uis
    [ "$status" -eq 0 ]

    refute_grep "^stop lightdm" "$SYSTEMCTL_LOG"
    grep -q "disable lightdm" "$SYSTEMCTL_LOG"
    grep -qxF "systemd-dropin:lightdm.service" "$DISABLED_SERVICES_FILE"
    [ -f "$(competing_ui_dropin lightdm.service)" ]
}

@test "display manager: XDG_SESSION_TYPE=wayland alone marks a graphical session" {
    platform="k2"
    mock_unit_state "lightdm" 0 "enabled"
    XDG_SESSION_TYPE="wayland"

    run stop_competing_uis
    [ "$status" -eq 0 ]

    refute_grep "^stop lightdm" "$SYSTEMCTL_LOG"
}

@test "display manager: under sudo, a desktop ancestor still marks a graphical session" {
    # `curl ... | sudo sh` resets the environment: the installer itself has no
    # DISPLAY, but the terminal's shell two levels up does.
    platform="k2"
    mock_unit_state "lightdm" 0 "enabled"
    mock_proc $$ 4000 "PATH=/usr/bin" "HOME=/root"
    mock_proc 4000 3000 "SUDO_USER=pi"
    mock_proc 3000 1 "DISPLAY=:0" "HOME=/home/pi"

    run stop_competing_uis
    [ "$status" -eq 0 ]

    refute_grep "^stop lightdm" "$SYSTEMCTL_LOG"
    grep -q "disable lightdm" "$SYSTEMCTL_LOG"
}

@test "display manager: an ssh chain with no display ancestor stops the DM" {
    platform="k2"
    mock_unit_state "lightdm" 0 "enabled"
    mock_proc $$ 4000 "PATH=/usr/bin"
    mock_proc 4000 3000 "SSH_CONNECTION=1.2.3.4 5 6.7.8.9 22"
    mock_proc 3000 1 "DISPLAY="

    run stop_competing_uis
    [ "$status" -eq 0 ]

    grep -q "^stop lightdm" "$SYSTEMCTL_LOG"
}

@test "display manager: an unreadable /proc reads as not graphical" {
    platform="k2"
    mock_unit_state "lightdm" 0 "enabled"
    HELIX_PROC_ROOT="$BATS_TEST_TMPDIR/no-such-proc"

    run stop_competing_uis
    [ "$status" -eq 0 ]

    grep -q "^stop lightdm" "$SYSTEMCTL_LOG"
}

@test "display manager: a disabled sddm is left alone" {
    platform="k2"
    mock_unit_state "sddm" 3 "disabled"

    run stop_competing_uis
    [ "$status" -eq 0 ]

    refute_grep "disable sddm" "$SYSTEMCTL_LOG"
}

# --- round trip: what the sweep records, uninstall re-enables ---

@test "round trip: the drop-in is removed and reloaded before the unit is re-enabled" {
    mock_unit_state "KlipperScreen" 3 "enabled"

    run stop_competing_uis
    [ "$status" -eq 0 ]
    local dropin
    dropin=$(competing_ui_dropin KlipperScreen.service)
    [ -f "$dropin" ]

    : > "$SYSTEMCTL_LOG"
    run reenable_disabled_services
    [ "$status" -eq 0 ]

    [ ! -e "$dropin" ]
    [ ! -e "$(dirname "$dropin")" ]
    [ "$(grep -n "^daemon-reload" "$SYSTEMCTL_LOG" | cut -d: -f1)" -lt \
      "$(grep -n "^enable KlipperScreen" "$SYSTEMCTL_LOG" | cut -d: -f1)" ]
}

@test "round trip: an old disable-only record replays as a plain enable" {
    echo "systemd:KlipperScreen" > "$DISABLED_SERVICES_FILE"
    mock_unit_state "KlipperScreen" 3 "disabled"

    run reenable_disabled_services
    [ "$status" -eq 0 ]

    grep -q "^enable KlipperScreen" "$SYSTEMCTL_LOG"
    refute_grep "daemon-reload" "$SYSTEMCTL_LOG"
}

@test "round trip: an enabled but stopped KlipperScreen is re-enabled on uninstall" {
    mock_unit_state "KlipperScreen" 3 "enabled"

    run stop_competing_uis
    [ "$status" -eq 0 ]
    grep -qxF "systemd-dropin:KlipperScreen.service" "$DISABLED_SERVICES_FILE"

    : > "$SYSTEMCTL_LOG"
    run reenable_disabled_services
    [ "$status" -eq 0 ]

    grep -q "enable KlipperScreen" "$SYSTEMCTL_LOG"
}
