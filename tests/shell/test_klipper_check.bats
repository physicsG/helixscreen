#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for check_klipper_ecosystem() in scripts/lib/installer/requirements.sh

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

setup() {
    load helpers
    # tty_confirm, which the prompt uses
    . "$WORKTREE_ROOT/scripts/lib/installer/common.sh"

    # Override log stubs so we can assert on output
    log_error()   { echo "ERROR: $*"; }
    log_info()    { echo "INFO: $*"; }
    log_warn()    { echo "WARN: $*"; }
    log_success() { echo "OK: $*"; }
    export -f log_error log_info log_warn log_success

    # Reset source guard
    unset _HELIX_REQUIREMENTS_SOURCED
    . "$WORKTREE_ROOT/scripts/lib/installer/requirements.sh"
    # Where the reachability probe finds Moonraker's URL, and how it asks.
    unset _HELIX_HOST_PROFILE_SOURCED _HELIX_PRINT_CONF_SOURCED _HELIX_MOONRAKER_SOURCED _HELIX_RELEASE_SOURCED
    . "$WORKTREE_ROOT/scripts/lib/installer/host_profile.sh"
    . "$WORKTREE_ROOT/scripts/lib/installer/helix_print_conf.sh"
    . "$WORKTREE_ROOT/scripts/lib/installer/moonraker.sh"
    . "$WORKTREE_ROOT/scripts/lib/installer/release.sh"
    export KLIPPER_HOME="$BATS_TEST_TMPDIR/home" HELIX_PROC_ROOT="$BATS_TEST_TMPDIR/proc"
    mkdir -p "$HELIX_PROC_ROOT"
    unset MOONRAKER_URL
}

# Helper: write a ps mock that outputs the given file content
# Args: file containing ps output lines
setup_ps_mock() {
    local ps_file="$1"
    mkdir -p "$BATS_TEST_TMPDIR/bin"
    cat > "$BATS_TEST_TMPDIR/bin/ps" <<MOCK
#!/bin/sh
cat "$ps_file"
MOCK
    chmod +x "$BATS_TEST_TMPDIR/bin/ps"
}

# Helper: run check_klipper_ecosystem in a subshell with controlled ps/wget
# Args: platform, ps_content_file (empty keeps a ps the test mocked itself),
#       wget_exit_code (0=success)
run_klipper_check() {
    local platform="$1"
    local ps_file="$2"
    local wget_exit="${3:-0}"

    mkdir -p "$BATS_TEST_TMPDIR/bin"

    [ -z "$ps_file" ] || setup_ps_mock "$ps_file"

    # Mock wget
    printf '#!/bin/sh\nexit %d\n' "$wget_exit" > "$BATS_TEST_TMPDIR/bin/wget"
    chmod +x "$BATS_TEST_TMPDIR/bin/wget"

    # Hide curl so wget path is taken
    printf '#!/bin/sh\nexit 127\n' > "$BATS_TEST_TMPDIR/bin/curl"
    chmod +x "$BATS_TEST_TMPDIR/bin/curl"

    export PATH="$BATS_TEST_TMPDIR/bin:$PATH"

    # No terminal to ask: helpers.bash points HELIX_TTY_DEVICE at nothing.
    run check_klipper_ecosystem "$platform"
}

# Fake a process under HELIX_PROC_ROOT running the given argv.
# Args: $1 = pid, $2.. = argv
mock_proc_cmdline() {
    local pid="$1"
    shift
    mkdir -p "$HELIX_PROC_ROOT/$pid"
    printf '%s\0' "$@" > "$HELIX_PROC_ROOT/$pid/cmdline"
}

# procps-ng ps: bare, it lists only the installer's own session; -e lists all.
# Args: $1 = file holding what `ps -e -o args` prints
mock_procps_ps() {
    mock_command_script ps "
case \"\$1\" in
    -e) cat '$1' ;;
    *) printf '  PID TTY          TIME CMD\\n 9001 pts/0    00:00:00 sh\\n' ;;
esac"
}

# A systemd host whose systemctl answers is-active from the units named in $1
# (space separated) and lists them for list-units.
mock_systemd_units() {
    INIT_SYSTEM=systemd
    mock_command_script systemctl "
active='$1'
case \"\$1\" in
    is-active)
        for u in \$active; do [ \"\$u\" = \"\$3\" ] && exit 0; done
        exit 3 ;;
    list-units)
        for u in \$active; do printf '%s loaded active running unit\\n' \"\$u\"; done ;;
esac
exit 0"
}

# ===========================================================================
# Platform filtering
# ===========================================================================

@test "check_klipper_ecosystem: skips on pi platform" {
    run check_klipper_ecosystem "pi"
    [ "$status" -eq 0 ]
    [ -z "$output" ]
}

@test "check_klipper_ecosystem: skips on pi32 platform" {
    run check_klipper_ecosystem "pi32"
    [ "$status" -eq 0 ]
    [ -z "$output" ]
}

# ===========================================================================
# Happy path: both services running and responsive
# ===========================================================================

@test "check_klipper_ecosystem: passes when both klipper and moonraker running" {
    local ps_file="$BATS_TEST_TMPDIR/ps_output"
    cat > "$ps_file" <<'EOF'
  PID USER     COMMAND
  123 root     klippy/klippy.py
  456 root     moonraker/moonraker.py
EOF

    run_klipper_check "ad5m" "$ps_file" 0
    [ "$status" -eq 0 ]
    [[ "$output" == *"Klipper and Moonraker are running"* ]]
}

@test "check_klipper_ecosystem: works for k1 platform too" {
    local ps_file="$BATS_TEST_TMPDIR/ps_output"
    cat > "$ps_file" <<'EOF'
  PID USER     COMMAND
  123 root     klippy/klippy.py
  456 root     moonraker/moonraker.py
EOF

    run_klipper_check "k1" "$ps_file" 0
    [ "$status" -eq 0 ]
    [[ "$output" == *"Klipper and Moonraker are running"* ]]
}

# ===========================================================================
# Warning cases (non-interactive continues with warning)
# ===========================================================================

@test "check_klipper_ecosystem: warns when klipper not running" {
    local ps_file="$BATS_TEST_TMPDIR/ps_output"
    cat > "$ps_file" <<'EOF'
  PID USER     COMMAND
  456 root     moonraker/moonraker.py
EOF

    run_klipper_check "ad5m" "$ps_file" 0
    [ "$status" -eq 0 ]
    contains "Klipper does not appear to be running" "$output"
    contains "Continuing installation" "$output"
}

@test "check_klipper_ecosystem: warns when moonraker not running" {
    local ps_file="$BATS_TEST_TMPDIR/ps_output"
    cat > "$ps_file" <<'EOF'
  PID USER     COMMAND
  123 root     klippy/klippy.py
EOF

    run_klipper_check "ad5m" "$ps_file" 0
    [ "$status" -eq 0 ]
    contains "Moonraker does not appear to be running" "$output"
    contains "Continuing installation" "$output"
}

@test "check_klipper_ecosystem: warns when neither running" {
    local ps_file="$BATS_TEST_TMPDIR/ps_output"
    cat > "$ps_file" <<'EOF'
  PID USER     COMMAND
    1 root     init
EOF

    run_klipper_check "ad5m" "$ps_file" 0
    [ "$status" -eq 0 ]
    contains "Klipper does not appear to be running" "$output"
    [[ "$output" == *"Moonraker does not appear to be running"* ]]
}

@test "check_klipper_ecosystem: warns when moonraker running but not responding" {
    local ps_file="$BATS_TEST_TMPDIR/ps_output"
    cat > "$ps_file" <<'EOF'
  PID USER     COMMAND
  123 root     klippy/klippy.py
  456 root     moonraker/moonraker.py
EOF

    run_klipper_check "ad5m" "$ps_file" 1
    [ "$status" -eq 0 ]
    contains "not responding on http://127.0.0.1:7125" "$output"
    contains "Continuing installation" "$output"
}

# ===========================================================================
# The prompt reads the terminal device (tty_confirm); with none, it continues.
# test_installer_plan.bats drives the device and the dry-run skip.
# ===========================================================================

@test "check_klipper_ecosystem: with no terminal to ask, continues with warning" {
    local ps_file="$BATS_TEST_TMPDIR/ps_output"
    cat > "$ps_file" <<'EOF'
  PID USER     COMMAND
    1 root     init
EOF

    run_klipper_check "ad5m" "$ps_file" 0
    [ "$status" -eq 0 ]
    contains "Continuing installation" "$output"
}

@test "check_klipper_ecosystem: probes Moonraker on the port moonraker.conf names" {
    local ps_file="$BATS_TEST_TMPDIR/ps_output"
    cat > "$ps_file" <<'EOF'
  PID USER     COMMAND
  123 root     klippy/klippy.py
  456 root     moonraker/moonraker.py
EOF
    mkdir -p "$KLIPPER_HOME/printer_data/config"
    printf '[server]\nport: 80\n' > "$KLIPPER_HOME/printer_data/config/moonraker.conf"

    run_klipper_check "ad5m" "$ps_file" 1
    [ "$status" -eq 0 ]
    contains "not responding on http://127.0.0.1:80" "$output"
}

@test "check_klipper_ecosystem: gives Moonraker 5 seconds to answer" {
    local ps_file="$BATS_TEST_TMPDIR/ps_output" log="$BATS_TEST_TMPDIR/wget.log"
    cat > "$ps_file" <<'EOF'
  PID USER     COMMAND
  123 root     klippy/klippy.py
  456 root     moonraker/moonraker.py
EOF
    run_klipper_check "ad5m" "$ps_file" 0
    printf '#!/bin/sh\nprintf "%%s\\n" "$*" >> "%s"\n' "$log" > "$BATS_TEST_TMPDIR/bin/wget"

    run check_klipper_ecosystem "ad5m"
    [ "$status" -eq 0 ]
    grep -q -- '-T 5 .*server/info' "$log" || fail "probe: $(cat "$log")"
}

# ===========================================================================
# Hosts whose bare ps does not list every process
# ===========================================================================

@test "check_klipper_ecosystem: procps ps hiding other sessions, systemd units active -> running" {
    mock_command_script ps "printf '  PID TTY          TIME CMD\\n 9001 pts/0    00:00:00 sh\\n'"
    mock_systemd_units "klipper.service moonraker.service"

    run_klipper_check "k2" "" 0
    [ "$status" -eq 0 ]
    contains "Klipper and Moonraker are running" "$output"
}

@test "check_klipper_ecosystem: numbered systemd instances count, klipper-mcu does not" {
    mock_command_script ps "printf '  PID TTY          TIME CMD\\n 9001 pts/0    00:00:00 sh\\n'"
    mock_systemd_units "klipper-mcu.service moonraker-1.service"

    run_klipper_check "k2" "" 0
    [ "$status" -eq 0 ]
    contains "Klipper does not appear to be running" "$output"
    lacks "Moonraker does not appear to be running" "$output"
}

@test "check_klipper_ecosystem: procps ps lists Klipper and Moonraker under -e -> running" {
    local full="$BATS_TEST_TMPDIR/ps_all"
    cat > "$full" <<'EOF2'
/usr/share/klippy-env/bin/python /usr/share/klipper/klippy/klippy.py /mnt/UDISK/printer_data/config/printer.cfg
/usr/share/moonraker-env/bin/python /usr/share/moonraker/moonraker/moonraker.py
EOF2
    mock_procps_ps "$full"
    mock_systemd_units ""

    run_klipper_check "k2" "" 0
    [ "$status" -eq 0 ]
    contains "Klipper and Moonraker are running" "$output"
}

@test "check_klipper_ecosystem: BusyBox ps that rejects -e still lists klippy under w -> running" {
    local ps_file="$BATS_TEST_TMPDIR/ps_output"
    cat > "$ps_file" <<'EOF2'
  PID USER       VSZ STAT COMMAND
  123 root     40000 S    python3 /usr/share/klipper/klippy/klippy.py /usr/data/printer_data/config/printer.cfg
  456 root     30000 S    python3 /usr/data/moonraker/moonraker/moonraker.py
EOF2
    mock_command_script ps "case \"\$1\" in -e) exit 1 ;; w) cat '$ps_file' ;; *) echo '  PID USER COMMAND' ;; esac"

    run_klipper_check "k1" "" 0
    [ "$status" -eq 0 ]
    contains "Klipper and Moonraker are running" "$output"
}

@test "check_klipper_ecosystem: Klipper and Moonraker in /proc, ps showing neither -> running" {
    mock_command_script ps "echo '  PID USER COMMAND'"
    mock_proc_cmdline 123 /usr/bin/python3 /usr/share/klipper/klippy/klippy.py printer.cfg
    mock_proc_cmdline 456 /usr/bin/python3 /usr/data/moonraker/moonraker/moonraker.py

    run_klipper_check "k1" "" 0
    [ "$status" -eq 0 ]
    contains "Klipper and Moonraker are running" "$output"
}

@test "check_klipper_ecosystem: nothing in /proc, ps or systemd -> warns about both" {
    mock_procps_ps /dev/null
    mock_systemd_units ""

    run_klipper_check "k2" "" 0
    [ "$status" -eq 0 ]
    contains "Klipper does not appear to be running" "$output"
    contains "Moonraker does not appear to be running" "$output"
}

@test "check_klipper_ecosystem: no systemctl and no pgrep -> ps alone decides" {
    local ps_file="$BATS_TEST_TMPDIR/ps_output"
    cat > "$ps_file" <<'EOF2'
  PID USER     COMMAND
  123 root     klippy/klippy.py
  456 root     moonraker/moonraker.py
EOF2
    INIT_SYSTEM=sysv
    mock_command_fail systemctl
    mock_command_script pgrep "exit 127"

    run_klipper_check "ad5m" "$ps_file" 0
    [ "$status" -eq 0 ]
    contains "Klipper and Moonraker are running" "$output"
}

# ===========================================================================
# Processes that only name Klipper or Moonraker are neither
# ===========================================================================

@test "check_klipper_ecosystem: a klippy.log tail, an editor, KlipperScreen and moonraker-obico in /proc -> warns about both" {
    mock_command_script ps "echo '  PID USER COMMAND'"
    mock_proc_cmdline 201 tail -f /usr/data/printer_data/logs/klippy.log
    mock_proc_cmdline 202 vi /usr/share/klipper/klippy/klippy.py
    mock_proc_cmdline 203 /usr/bin/python3 /home/pi/KlipperScreen/screen.py
    mock_proc_cmdline 204 /usr/bin/python3 -m moonraker_obico.app -c /home/pi/printer_data/config/moonraker-obico.cfg
    mock_proc_cmdline 205 /usr/bin/python3 /home/pi/moonraker-telegram-bot/bot/main.py

    run_klipper_check "k1" "" 0
    [ "$status" -eq 0 ]
    contains "Klipper does not appear to be running" "$output"
    contains "Moonraker does not appear to be running" "$output"
}

@test "check_klipper_ecosystem: the same processes in a ps listing -> warns about both" {
    local ps_file="$BATS_TEST_TMPDIR/ps_output"
    cat > "$ps_file" <<'EOF2'
  PID USER     COMMAND
  201 root     tail -f /usr/data/printer_data/logs/klippy.log
  203 root     /usr/bin/python3 /home/pi/KlipperScreen/screen.py
  204 root     /usr/bin/python3 -m moonraker_obico.app -c moonraker-obico.cfg
  205 root     /usr/bin/python3 /home/pi/moonraker-telegram-bot/bot/main.py
  206 root     klipper_mcu -w
EOF2

    run_klipper_check "ad5m" "$ps_file" 0
    [ "$status" -eq 0 ]
    contains "Klipper does not appear to be running" "$output"
    contains "Moonraker does not appear to be running" "$output"
}

@test "check_klipper_ecosystem: a ps listing is not consulted while /proc lists processes" {
    local ps_file="$BATS_TEST_TMPDIR/ps_output"
    cat > "$ps_file" <<'EOF2'
  PID USER     COMMAND
  123 root     python3 klippy/klippy.py
  456 root     python3 moonraker/moonraker.py
EOF2
    mock_proc_cmdline 1 /sbin/init

    run_klipper_check "ad5m" "$ps_file" 0
    [ "$status" -eq 0 ]
    contains "Klipper does not appear to be running" "$output"
    contains "Moonraker does not appear to be running" "$output"
}

# ===========================================================================
# The HTTP probe follows the process probes
# ===========================================================================

@test "check_klipper_ecosystem: Moonraker in /proc, bare ps showing nothing, not answering -> not responding" {
    mock_command_script ps "printf '  PID TTY          TIME CMD\\n 9001 pts/0    00:00:00 sh\\n'"
    mock_proc_cmdline 123 /usr/bin/python3 /usr/share/klipper/klippy/klippy.py printer.cfg
    mock_proc_cmdline 456 /usr/bin/python3 /usr/data/moonraker/moonraker/moonraker.py

    run_klipper_check "k2" "" 1
    [ "$status" -eq 0 ]
    contains "Moonraker is running but not responding on http://127.0.0.1:7125" "$output"
    lacks "Moonraker does not appear to be running" "$output"
}

@test "check_klipper_ecosystem: a ps line naming moonraker-obico, Moonraker absent -> no HTTP probe" {
    local ps_file="$BATS_TEST_TMPDIR/ps_output"
    cat > "$ps_file" <<'EOF2'
  PID USER     COMMAND
  123 root     python3 klippy/klippy.py
  204 root     /usr/bin/python3 -m moonraker_obico.app -c moonraker-obico.cfg
EOF2
    mock_proc_cmdline 123 /usr/bin/python3 /usr/share/klipper/klippy/klippy.py printer.cfg

    run_klipper_check "k1" "$ps_file" 1
    [ "$status" -eq 0 ]
    contains "Moonraker does not appear to be running" "$output"
    lacks "not responding" "$output"
}
