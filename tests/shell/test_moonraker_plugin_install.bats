#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Guards for moonraker-plugin/install.sh and helix_print.py: the installer
# and plugin stay phase-tracking-free, and every uninstall path
# (auto_uninstall and the interactive uninstall) strips any phase-tracking
# instrumentation a pre-1.1 HelixScreen wrote into PRINT_START, leaving the
# shared helix_macros.cfg and everything else in the config tree untouched.
# The marker-matching and safe-write logic itself lives in
# strip_phase_tracking.py and is pinned by moonraker-plugin/tests/
# test_strip_phase_tracking.py; these tests cover the shell-side wiring.

load helpers

REPO_ROOT="$(cd "${BATS_TEST_DIRNAME}/../.." && pwd)"

# Overridable so each guard can be proven to FIRE against a hand-broken copy
# of the script instead of the shipped one.
SCRIPT="${HELIX_PLUGIN_INSTALL_SH:-$REPO_ROOT/moonraker-plugin/install.sh}"
PLUGIN_PY="${HELIX_PLUGIN_PRINT_PY:-$REPO_ROOT/moonraker-plugin/helix_print.py}"
STRIP_PY="${HELIX_STRIP_PY:-$REPO_ROOT/moonraker-plugin/strip_phase_tracking.py}"

# A real, four-space-indent instrumented PRINT_START, shaped exactly like a
# pre-1.1 HelixScreen's own writer produced it (marker pairs after every
# matched phase line, plus the trailing HELIX_READY block) - not a shape a
# fixture only vaguely resembling one.
write_instrumented_printer_cfg() {
    cat > "$1" <<'EOF'
[include helix_macros.cfg]

[printer]
kinematics: corexy

[gcode_macro PRINT_START]
description: Start print
gcode:
    {% set BED_TEMP = params.BED_TEMP|default(60)|float %}
    {% set EXTRUDER_TEMP = params.EXTRUDER_TEMP|default(200)|float %}
    M190 S{BED_TEMP}
    # <<< HELIX_TRACKING v2 >>>
    HELIX_PHASE_HEATING_BED
    # <<< /HELIX_TRACKING >>>
    G28
    # <<< HELIX_TRACKING v2 >>>
    HELIX_PHASE_HOMING
    # <<< /HELIX_TRACKING >>>

    QUAD_GANTRY_LEVEL
    # <<< HELIX_TRACKING v2 >>>
    HELIX_PHASE_QGL
    # <<< /HELIX_TRACKING >>>
    BED_MESH_CALIBRATE ADAPTIVE=1
    # <<< HELIX_TRACKING v2 >>>
    HELIX_PHASE_BED_MESH
    # <<< /HELIX_TRACKING >>>
    M109 S{EXTRUDER_TEMP}
    # <<< HELIX_TRACKING v2 >>>
    HELIX_PHASE_HEATING_NOZZLE
    # <<< /HELIX_TRACKING >>>
    CLEAN_NOZZLE
    # <<< HELIX_TRACKING v2 >>>
    HELIX_PHASE_CLEANING
    # <<< /HELIX_TRACKING >>>
    PURGE_LINE   ; prime
    # <<< HELIX_TRACKING v2 >>>
    HELIX_PHASE_PURGING
    # <<< /HELIX_TRACKING >>>

    # <<< HELIX_TRACKING v2 >>>
    HELIX_READY
    # <<< /HELIX_TRACKING >>>
[extruder]
step_pin: PA1

[heater_bed]
heater_pin: PB1
EOF
}

# The same macro, before instrumentation - what a correct strip must produce.
write_original_printer_cfg() {
    cat > "$1" <<'EOF'
[include helix_macros.cfg]

[printer]
kinematics: corexy

[gcode_macro PRINT_START]
description: Start print
gcode:
    {% set BED_TEMP = params.BED_TEMP|default(60)|float %}
    {% set EXTRUDER_TEMP = params.EXTRUDER_TEMP|default(200)|float %}
    M190 S{BED_TEMP}
    G28

    QUAD_GANTRY_LEVEL
    BED_MESH_CALIBRATE ADAPTIVE=1
    M109 S{EXTRUDER_TEMP}
    CLEAN_NOZZLE
    PURGE_LINE   ; prime

[extruder]
step_pin: PA1

[heater_bed]
heater_pin: PB1
EOF
}

# Build a scratch HOME shaped like a real printer's: a Moonraker checkout
# with the plugin symlinked in, and printer_data/config holding an
# instrumented printer.cfg plus the real helix_macros.cfg. Shims sudo,
# service and curl so auto_uninstall's restart/wait steps are no-ops;
# systemctl is shadowed inert by `load helpers` already.
setup_instrumented_home() {
    local home="$1"
    mkdir -p "$home/moonraker/moonraker/components" "$home/printer_data/config" "$home/plugin"
    : > "$home/plugin/helix_print.py"
    ln -s "$home/plugin/helix_print.py" "$home/moonraker/moonraker/components/helix_print.py"

    local c="$home/printer_data/config"
    cp "$REPO_ROOT/assets/config/helix_macros.cfg" "$c/helix_macros.cfg"
    printf '[server]\nhost: 0.0.0.0\n\n[helix_print]\n# HelixScreen plugin - auto-configured\n\n[octoprint_compat]\n' \
        > "$c/moonraker.conf"
    write_instrumented_printer_cfg "$c/printer.cfg"

    for cmd in sudo service curl; do
        mock_command_script "$cmd" 'exit 0'
    done
}

@test "install.sh passes POSIX syntax check" {
    [ -f "$SCRIPT" ]
    run sh -n "$SCRIPT"
    [ "$status" -eq 0 ]
}

@test "strip_phase_tracking.py passes a Python syntax check" {
    [ -f "$STRIP_PY" ]
    run python3 -m py_compile "$STRIP_PY"
    [ "$status" -eq 0 ]
}

@test "install.sh invokes strip_phase_tracking.py from SCRIPT_DIR on both uninstall paths" {
    # Confirms the wiring exists at all before the e2e tests below exercise
    # it by behaviour; this alone would not catch a commented-out call.
    grep -q 'strip_phase_tracking\.py' "$SCRIPT" || fail "install.sh never references strip_phase_tracking.py"
}

@test "uninstall preserves the shared helper macros (static)" {
    # helix_macros.cfg carries HELIX_START_PRINT / HELIX_CLEAN_NOZZLE and
    # other helpers that keep working without the plugin; deleting it on
    # uninstall would break features the user never uninstalled. Behavioural
    # proof (byte-identical after a real run) is in the e2e tests below.
    local auto_region="$BATS_TEST_TMPDIR/auto_uninstall.sh"
    local interactive_region="$BATS_TEST_TMPDIR/uninstall.sh"
    sed -n '/^auto_uninstall()/,/^}/p' "$SCRIPT" > "$auto_region"
    sed -n '/^uninstall()/,/^}/p' "$SCRIPT" > "$interactive_region"
    [ -s "$auto_region" ] || fail "auto_uninstall not found in $SCRIPT"
    [ -s "$interactive_region" ] || fail "uninstall not found in $SCRIPT"
    refute_grep 'rm.*helix_macros' "$auto_region"
    refute_grep 'rm.*helix_macros' "$interactive_region"
}

@test "installer carries no phase-tracking flag" {
    [ -f "$SCRIPT" ]
    refute_grep 'with-phase-tracking' "$SCRIPT"
    refute_grep 'ENABLE_PHASE_TRACKING' "$SCRIPT"
}

@test "plugin exposes no phase-tracking endpoints" {
    [ -f "$PLUGIN_PY" ]
    refute_grep 'phase_tracking' "$PLUGIN_PY"
}

# HelixScreen runs install.sh from a NoNewPrivileges service, where sudo can
# never work; it passes --no-restart and restarts Moonraker over its API.
@test "--no-restart leaves Moonraker alone on both auto paths" {
    local home="$BATS_TEST_TMPDIR/home"
    setup_instrumented_home "$home"
    local log="$BATS_TEST_TMPDIR/sudo.log"
    mock_command_script "sudo" "echo \"\$*\" >> '$log'"
    mock_command_script "service" "echo \"service \$*\" >> '$log'"

    run env HOME="$home" sh "$SCRIPT" --no-restart --uninstall-auto
    [ "$status" -eq 0 ]
    [ ! -e "$log" ] || fail "uninstall restarted Moonraker: $(cat "$log")"

    run env HOME="$home" sh "$SCRIPT" --no-restart --auto
    [ "$status" -eq 0 ]
    [ ! -e "$log" ] || fail "install restarted Moonraker: $(cat "$log")"
    grep -q '^\[helix_print\]' "$home/printer_data/config/moonraker.conf"
}

@test "without --no-restart the auto install still restarts Moonraker" {
    local home="$BATS_TEST_TMPDIR/home"
    setup_instrumented_home "$home"
    local log="$BATS_TEST_TMPDIR/sudo.log"
    mock_command_script "sudo" "echo \"\$*\" >> '$log'"
    mock_command_script "service" "echo \"service \$*\" >> '$log'"

    run env HOME="$home" sh "$SCRIPT" --auto
    [ "$status" -eq 0 ]
    grep -q 'restart' "$log" || fail "no restart attempted"
}

@test "auto_uninstall strips PRINT_START, preserves helix_macros.cfg, and removes the plugin" {
    local home="$BATS_TEST_TMPDIR/home"
    setup_instrumented_home "$home"
    local expected="$BATS_TEST_TMPDIR/expected_printer.cfg"
    write_original_printer_cfg "$expected"

    run env HOME="$home" sh "$SCRIPT" --uninstall-auto
    [ "$status" -eq 0 ]

    local c="$home/printer_data/config"
    diff "$expected" "$c/printer.cfg"
    cmp -s "$REPO_ROOT/assets/config/helix_macros.cfg" "$c/helix_macros.cfg" \
        || fail "helix_macros.cfg was modified by uninstall"
    [ ! -e "$home/moonraker/moonraker/components/helix_print.py" ]
    refute_grep '^\[helix_print\]' "$c/moonraker.conf"
}

@test "interactive uninstall strips PRINT_START and preserves helix_macros.cfg" {
    local home="$BATS_TEST_TMPDIR/home"
    setup_instrumented_home "$home"
    local expected="$BATS_TEST_TMPDIR/expected_printer.cfg"
    write_original_printer_cfg "$expected"

    run env HOME="$home" sh "$SCRIPT" --uninstall
    [ "$status" -eq 0 ]

    local c="$home/printer_data/config"
    diff "$expected" "$c/printer.cfg"
    cmp -s "$REPO_ROOT/assets/config/helix_macros.cfg" "$c/helix_macros.cfg" \
        || fail "helix_macros.cfg was modified by uninstall"
    [ ! -e "$home/moonraker/moonraker/components/helix_print.py" ]
    # The interactive path only prints a reminder; it does not touch
    # moonraker.conf.
    grep -q '^\[helix_print\]' "$c/moonraker.conf"
}

@test "auto_uninstall warns and continues when python3 is unavailable" {
    local home="$BATS_TEST_TMPDIR/home"
    setup_instrumented_home "$home"
    local before="$BATS_TEST_TMPDIR/before_printer.cfg"
    cp "$home/printer_data/config/printer.cfg" "$before"

    # A PATH with no python3 on it at all - every external command install.sh
    # itself calls, minus python3, plus the sudo/service/curl mocks above and
    # helpers' systemctl shim. The sandbox shim directory follows it so the
    # host sandbox's PATH layer still covers this callee, which is sh and so
    # carries none of its exported-function layer; the mocks come first and win.
    local no_py3="$BATS_TEST_TMPDIR/no-python3-path"
    mkdir -p "$no_py3"
    for tool in sh dirname cp mv rm ln grep awk date sleep; do
        p="$(command -v "$tool")" && ln -sf "$p" "$no_py3/$tool"
    done
    for cmd in sudo service curl systemctl; do
        ln -sf "$BATS_TEST_TMPDIR/bin/$cmd" "$no_py3/$cmd"
    done

    run env HOME="$home" PATH="$no_py3:$HELIX_TEST_SANDBOX_BIN" sh "$SCRIPT" --uninstall-auto
    # PRINT_START could not be checked at all, so this is "needs attention"
    # (2), not full success - the plugin itself is still gone.
    [ "$status" -eq 2 ]
    contains "python3 not found" "$output"

    # Nothing about PRINT_START was touched; only the symlink and
    # moonraker.conf section, which do not need python3, were removed.
    cmp -s "$before" "$home/printer_data/config/printer.cfg" \
        || fail "printer.cfg changed with no python3 available"
}

@test "auto_uninstall reports needs-attention when PRINT_START has an anomaly" {
    local home="$BATS_TEST_TMPDIR/home"
    setup_instrumented_home "$home"
    local c="$home/printer_data/config"
    # An unmatched BEGIN marker: strip_phase_tracking.py skips the file
    # rather than failing, but it is not clean either.
    printf '[gcode_macro PRINT_START]\ngcode:\n    G28\n    # <<< HELIX_TRACKING v2 >>>\n    HELIX_PHASE_HOMING\n' \
        > "$c/printer.cfg"
    local before="$BATS_TEST_TMPDIR/before_printer.cfg"
    cp "$c/printer.cfg" "$before"

    run env HOME="$home" sh "$SCRIPT" --uninstall-auto
    [ "$status" -eq 2 ]
    contains "next step" "$output"

    [ ! -e "$home/moonraker/moonraker/components/helix_print.py" ]
    cmp -s "$before" "$c/printer.cfg" || fail "the anomalous file was modified"
}

@test "interactive uninstall reports needs-attention when PRINT_START has an anomaly" {
    local home="$BATS_TEST_TMPDIR/home"
    setup_instrumented_home "$home"
    local c="$home/printer_data/config"
    printf '[gcode_macro PRINT_START]\ngcode:\n    G28\n    # <<< HELIX_TRACKING v2 >>>\n    HELIX_PHASE_HOMING\n' \
        > "$c/printer.cfg"

    run env HOME="$home" sh "$SCRIPT" --uninstall
    [ "$status" -eq 2 ]
    contains "next step" "$output"
    [ ! -e "$home/moonraker/moonraker/components/helix_print.py" ]
}

@test "auto_uninstall reports needs-attention when the strip cannot write a file" {
    [ "$(id -u)" -ne 0 ] || skip "running as root ignores the read-only mode"
    local home="$BATS_TEST_TMPDIR/home"
    setup_instrumented_home "$home"
    local c="$home/printer_data/config"
    # A second instrumented macro in a directory the strip cannot write to.
    # Chmod is scoped to this subdirectory, not the whole config tree, so the
    # top-level printer.cfg and moonraker.conf stay editable - only this
    # file's backup step fails.
    mkdir -p "$c/macros"
    write_instrumented_printer_cfg "$c/macros/print_start.cfg"
    chmod 0555 "$c/macros"

    run env HOME="$home" sh "$SCRIPT" --uninstall-auto
    chmod 0755 "$c/macros"  # restore so bats can clean up the tmpdir

    [ "$status" -eq 2 ]
    contains "failed to strip" "$output"
    [ ! -e "$home/moonraker/moonraker/components/helix_print.py" ]
}

@test "interactive uninstall reports needs-attention when the strip cannot write a file" {
    [ "$(id -u)" -ne 0 ] || skip "running as root ignores the read-only mode"
    local home="$BATS_TEST_TMPDIR/home"
    setup_instrumented_home "$home"
    local c="$home/printer_data/config"
    mkdir -p "$c/macros"
    write_instrumented_printer_cfg "$c/macros/print_start.cfg"
    chmod 0555 "$c/macros"

    run env HOME="$home" sh "$SCRIPT" --uninstall
    chmod 0755 "$c/macros"

    [ "$status" -eq 2 ]
    contains "failed to strip" "$output"
    [ ! -e "$home/moonraker/moonraker/components/helix_print.py" ]
}

@test "auto_uninstall reports a clean exit even when moonraker never comes back" {
    # curl always failing means wait_for_moonraker's own 30-attempt loop runs
    # to completion; sleep is stubbed to a no-op so that costs no real time.
    # A slow or unreachable Moonraker restart must not turn a completed,
    # clean uninstall into a reported failure.
    local home="$BATS_TEST_TMPDIR/home"
    setup_instrumented_home "$home"
    mock_command_script "curl" 'exit 1'
    mock_command_script "sleep" 'exit 0'
    local expected="$BATS_TEST_TMPDIR/expected_printer.cfg"
    write_original_printer_cfg "$expected"

    run env HOME="$home" sh "$SCRIPT" --uninstall-auto

    [ "$status" -eq 0 ]
    contains "did not come back up in time" "$output"
    contains "plugin removal itself completed" "$output"
    local c="$home/printer_data/config"
    diff "$expected" "$c/printer.cfg"
    [ ! -e "$home/moonraker/moonraker/components/helix_print.py" ]
}

@test "auto_uninstall's needs-attention outcome survives an unreachable moonraker" {
    local home="$BATS_TEST_TMPDIR/home"
    setup_instrumented_home "$home"
    local c="$home/printer_data/config"
    printf '[gcode_macro PRINT_START]\ngcode:\n    G28\n    # <<< HELIX_TRACKING v2 >>>\n    HELIX_PHASE_HOMING\n' \
        > "$c/printer.cfg"
    mock_command_script "curl" 'exit 1'
    mock_command_script "sleep" 'exit 0'

    run env HOME="$home" sh "$SCRIPT" --uninstall-auto

    [ "$status" -eq 2 ]
    contains "next step" "$output"
    contains "did not come back up in time" "$output"
    [ ! -e "$home/moonraker/moonraker/components/helix_print.py" ]
}

@test "auto_uninstall reports needs-attention when moonraker.conf cannot be backed up" {
    [ "$(id -u)" -ne 0 ] || skip "running as root ignores the read-only mode"
    local home="$BATS_TEST_TMPDIR/home"
    setup_instrumented_home "$home"
    local c="$home/printer_data/config"
    # A clean (non-instrumented) printer.cfg so the phase-tracking strip has
    # nothing to write and exits 0 on its own - isolating this test to the
    # moonraker.conf backup/edit step.
    write_original_printer_cfg "$c/printer.cfg"
    chmod 0555 "$c"

    run env HOME="$home" sh "$SCRIPT" --uninstall-auto
    chmod 0755 "$c"  # restore so bats can clean up the tmpdir

    [ "$status" -eq 2 ]
    contains "Could not back up moonraker.conf" "$output"
    [ ! -e "$home/moonraker/moonraker/components/helix_print.py" ]
}
