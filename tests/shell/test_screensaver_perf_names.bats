#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The screensaver measurement scripts (scripts/screensaver-perf/): what perf_env.sh sends to a
# device and the names and helpers it shares with them. A device script reaches the board as
# one stream, the KEY=VALUE prologue, then device_lib.sh, then its body, so the helpers it calls
# exist before it runs. A fake ssh runs that stream here instead of on a board.

ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"
PERF="$ROOT/scripts/screensaver-perf"
LIB="$PERF/device_lib.sh"

setup() {
    load helpers
    export HELIX_PERF_HOST=device.invalid
    export HELIX_PERF_SCRATCH="$BATS_TEST_TMPDIR/scratch"
    unset HELIX_PERF_PASSWORD HELIX_PERF_INSTALL HELIX_PERF_CTL_SOCK
    # Runs the command ssh was given (its last argument) locally, on ssh's stdin.
    mock_command_script ssh 'for arg; do last=$arg; done
printf "%s\n" "$last" >> "$BATS_TEST_TMPDIR/ssh-commands"
exec sh -c "$last"'
}

perf_env() {
    # shellcheck source=/dev/null
    . "$PERF/perf_env.sh"
}

body() {
    printf '%s\n' "$@" > "$BATS_TEST_TMPDIR/body.sh"
    echo "$BATS_TEST_TMPDIR/body.sh"
}

# Runs device_lib.sh then the given lines under POSIX sh, as a BusyBox board would.
lib_sh() {
    { cat "$LIB"; printf '%s\n' "$@"; } | sh
}

@test "a remote script is the prologue, then device_lib.sh, then the body" {
    perf_env
    script=$(body 'echo body ran')
    run perf_remote_script "$script" A=1
    [ "$status" -eq 0 ]
    expected=$(printf 'PW=%q\nINSTALL=%q\nCTL_SOCK=%q\nA=1\n' '' /home/pi/helixscreen \
        /run/helixscreen/control.sock; cat "$LIB" "$script")
    [ "$output" = "$expected" ]
}

@test "perf_run_remote runs the script under bash with its values and the helpers defined" {
    perf_env
    script=$(body 'echo "extra=[$ENV_EXTRA] install=[$INSTALL]"' 'type gate_raw > /dev/null && echo helpers')
    run perf_run_remote "$script" "ENV_EXTRA=A=1 B='two words'"
    [ "$status" -eq 0 ]
    [ "${lines[0]}" = "extra=[A=1 B='two words'] install=[/home/pi/helixscreen]" ]
    [ "${lines[1]}" = "helpers" ]
    [ "$(cat "$BATS_TEST_TMPDIR/ssh-commands")" = "bash -s" ]
}

@test "pi3b_service collect prints the file and removes it as root, through the fake ssh" {
    perf_env
    mock_command_script sudo '[ "$1" = "-n" ] && shift; exec "$@"'
    printf 'hist line\n' > "$BATS_TEST_TMPDIR/hist.txt"
    run perf_run_remote "$PERF/pi3b_service.sh" ACTION=collect "FILE=$BATS_TEST_TMPDIR/hist.txt"
    [ "$status" -eq 0 ]
    [ "$output" = "hist line" ]
    [ ! -e "$BATS_TEST_TMPDIR/hist.txt" ]
}

@test "sudo_ feeds the password on stdin and never on the command line" {
    mock_command_script sudo 'echo "args=$*"; echo "stdin=$(cat)"'
    run lib_sh 'PW=s3cret' 'sudo_ true'
    [ "$status" -eq 0 ]
    [ "${lines[0]}" = "args=-S -p  true" ]
    [ "${lines[1]}" = "stdin=s3cret" ]
    run lib_sh 'PW=' 'sudo_ true'
    [ "${lines[0]}" = "args=-n true" ]
}

@test "stat_ticks sums utime and stime when the command name holds spaces and parentheses" {
    printf '%s\n' '812 (helix (x) screen) S 1 812 812 0 -1 4194560 100 0 0 0 1234 56 0 0 20 0 9 0 99' \
        > "$BATS_TEST_TMPDIR/stat"
    run lib_sh "stat_ticks '$BATS_TEST_TMPDIR/stat'"
    [ "$status" -eq 0 ]
    [ "$output" = "1290" ]
}

@test "cpu_pct is percent of one core and clk_tck falls back to 100 without getconf" {
    run lib_sh 'cpu_pct 300 100 60'
    [ "$output" = "5.0" ]
    mock_command_fail getconf
    run lib_sh 'clk_tck'
    [ "$output" = "100" ]
}

@test "helix_pid takes the first helix-screen process from ps, and pidof when ps has no -o" {
    mock_command_script ps 'printf "  PID COMMAND\n  10 helix-watchdog\n  42 helix-screen\n  50 helix-screen-fbdev\n"'
    run lib_sh 'helix_pid'
    [ "$output" = "42" ]
    mock_command_script ps 'echo "ps: invalid option -- e" >&2; exit 1'
    mock_command_script pidof '[ "$*" = "helix-screen" ] && echo "77 76"'
    run lib_sh 'helix_pid'
    [ "$output" = "77" ]
}

@test "gate_raw prints the field list ctparse.py reads, with none for an empty error" {
    run lib_sh 'gate_raw /tmp/h.txt 0 "" 4.2 1 0'
    [ "$output" = "GATE_RAW hist=/tmp/h.txt ct_exit=0 ct_err=none helix_cpu=4.2 saver_started=1 saver_stopped_midrun=0" ]
    run python3 "$PERF/ctparse.py" /dev/null arm off "$output"
    [[ "$output" == *"helix_cpu=4.2% saver_started=1 saver_stopped_midrun=0 ct_exit=0 ct_err=none" ]]
}

@test "perf_record_gate appends one prefixed RESULT line to the arm's results" {
    perf_env
    printf '000100 000010\n# Histogram Overflows: 00000\n# Max Latencies: 00100\n' > "$BATS_TEST_TMPDIR/hist"
    perf_record_gate armA 2 toasters "$BATS_TEST_TMPDIR/hist" "GATE_RAW ct_exit=0 helix_cpu=3.0"
    perf_record_gate armA 2 off "$BATS_TEST_TMPDIR/hist" ""
    run cat "$HELIX_PERF_SCRATCH/results/armA.txt"
    [ "${#lines[@]}" -eq 2 ]
    [[ "${lines[0]}" == "armA run=2 gate RESULT arm=armA saver=toasters samples=10 "*"helix_cpu=3.0%"* ]] || fail "toasters RESULT line: ${lines[0]}"
    [[ "${lines[1]}" == "armA run=2 gate RESULT arm=armA saver=off "*"helix_cpu=-1%"* ]]
}

# Every stable name in the kScreensavers table, read independently of perf_env.sh: inside the
# table the only all-lowercase string literals are the names (labels are capitalised).
registry_names() {
    awk '/kScreensavers\[\] = \{/ {inside = 1; next} inside && /^\};/ {exit} inside' \
        "${1:-$ROOT/include/screensaver_registry.h}" | grep -oE '"[a-z0-9_]+"' | tr -d '"'
}

@test "perf_saver_name accepts every name the registry defines, and nothing else" {
    perf_env
    names=$(registry_names)
    rows=$(awk '/kScreensavers\[\] = \{/ {inside = 1; next} inside && /^\};/ {exit} inside' \
        "$ROOT/include/screensaver_registry.h" | grep -c 'ScreensaverType::')
    [ "$(echo "$names" | wc -l)" -eq "$rows" ] || fail "registry_names found $(echo "$names" | wc -l) of $rows rows"
    contains fireflies "$names" || return 1
    for name in $names; do
        run perf_saver_name "$name"
        [ "$status" -eq 0 ] && [ "$output" = "$name" ] || fail "rejected registry name '$name': $output"
    done
    [ "$(perf_saver_names | sort)" = "$(echo "$names" | sort)" ] || fail "perf_saver_names: $(perf_saver_names)"
    run perf_saver_name toaster
    [ "$status" -ne 0 ]
    contains "unknown workload 'toaster'" "$output"
}

@test "perf_saver_name maps idle, off and ui to off" {
    perf_env
    for workload in idle off ui; do
        run perf_saver_name "$workload"
        [ "$status" -eq 0 ] && [ "$output" = off ] || fail "$workload gave '$output'"
    done
}

@test "a saver added to the registry is a workload with no edit to the perf scripts" {
    perf_env
    PERF_REGISTRY="$BATS_TEST_TMPDIR/registry.h"
    # Two rows after fireflies, one wrapped the way clang-format wraps a long row.
    awk '{ print } /"fireflies"/ {
        print "    {ScreensaverType::AQUA, \"aqua\", \"Aqua Surface\", SAVER_DEPTH_16 | SAVER_DEPTH_32},"
        print "    {ScreensaverType::HIGHFLY,"
        print "     \"highfly\", \"High Fly\", SAVER_DEPTH_16 | SAVER_DEPTH_32},"
    }' "$ROOT/include/screensaver_registry.h" > "$PERF_REGISTRY"
    contains '"highfly"' "$(cat "$PERF_REGISTRY")" || return 1
    for name in aqua highfly fireflies; do
        run perf_saver_name "$name"
        [ "$status" -eq 0 ] && [ "$output" = "$name" ] || fail "rejected '$name': $output"
    done
}

@test "perf_saver_name refuses to guess when the registry cannot be read" {
    perf_env
    PERF_REGISTRY="$BATS_TEST_TMPDIR/missing.h"
    run perf_saver_name toasters
    [ "$status" -ne 0 ]
    contains "no screensaver names in $PERF_REGISTRY" "$output"
}

@test "saver_starts and saver_stops count only the manager's start and stop lines" {
    mock_command_script sudo '[ "$1" = "-n" ] && shift; exec "$@"'
    mock_command_script journalctl 'echo "args=$*" > "$BATS_TEST_TMPDIR/journal-args"
printf "%s\n" "[ScreensaverManager] Started screensaver type 3" "[Screensaver] Stopping type 3" \
    "[ScreensaverManager] Stopped screensaver type 3 (black screen)" "[Wifi] Stopping scan" \
    "[ScreensaverManager] Started screensaver type 5"'
    run lib_sh 'echo "starts=$(saver_starts "2026-10-16 10:00:00")"' 'echo "stops=$(saver_stops "2026-10-16 10:00:00")"'
    [ "$status" -eq 0 ]
    [ "${lines[0]}" = "starts=2" ]
    [ "${lines[1]}" = "stops=1" ]
    contains "--since 2026-10-16 10:00:00" "$(cat "$BATS_TEST_TMPDIR/journal-args")"
}

@test "the measure and load-gate steps count saver starts and stops through device_lib.sh" {
    for step in pi3b_measure.sh pi3b_loadgate.sh; do
        contains 'saver_stops "$T_START"' "$(cat "$PERF/$step")" || return 1
        contains 'saver_starts "$T_START"' "$(cat "$PERF/$step")" || return 1
        lacks 'journalctl' "$(cat "$PERF/$step")" || return 1
    done
}
