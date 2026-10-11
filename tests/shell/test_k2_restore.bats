#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The K2 stock-UI restore at uninstall (prestonbrown/helixscreen#1641).
#
# restore_previous_ui_platform puts /etc/init.d/app back: it kills any
# web-server the carve-out left running, enables app, verifies the S (boot)
# link, and starts app for the session. Each case drives a GENERATED bundle,
# uninstall.sh or install.sh, under /bin/sh, because those are
# what a user runs. The mock root supplies:
#   - an rc.common stand-in that logs every dispatch to $EVENTS and writes
#     only the rc.d links a case asks for
#   - a pidof that reports web-server as $FAKE_WEBSERVER_PID until it is killed
#   - a shell-function kill that logs to $EVENTS instead of signalling
# The one real signal is 1g's: a pidfile-scoped stop of a process this test
# owns.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

# The web-server pid pidof reports. It is above pid_max on Linux (at most
# 4194304) and macOS, so a kill that escapes the shadow reaches no process.
LIVE_WEBSERVER_PID=9999999

# The host paths patch_bundle rewrites into MOCK_ROOT, as one ERE alternation.
REDIRECTED_HOST_PATHS='/etc/init\.d/|/etc/rc\.d/|/etc/rc\.common|/mnt/UDISK|/usr/bin/update-cosmos|/var/run/helix|/var/tmp/helix|/tmp/helix|/opt/\._helixscreen|/root/\._helixscreen'

setup() {
    load helpers
    install_gnu_sed_shim

    export MOCK_ROOT="$BATS_TEST_TMPDIR/root"
    mkdir -p "$MOCK_ROOT/etc/init.d" "$MOCK_ROOT/etc/rc.d" "$MOCK_ROOT/usr/bin" \
             "$MOCK_ROOT/var/run" "$MOCK_ROOT/var/tmp" "$MOCK_ROOT/tmp"

    export INSTALL_DIR="$BATS_TEST_TMPDIR/install/helixscreen"
    mkdir -p "$INSTALL_DIR/config"
    export HELIX_STATE_VAR_LIB="$BATS_TEST_TMPDIR/var/lib/helixscreen"
    export HELIX_STATE_ROOT_HOME="$BATS_TEST_TMPDIR/root-home/.helixscreen"

    export EVENTS="$BATS_TEST_TMPDIR/events.log"
    export WEB_KILLED="$BATS_TEST_TMPDIR/web-server.killed"
    # rc.common's `enable` writes these rc.d slots; `start` exits with this.
    export RC_ENABLE_LINKS="S99 K01"
    export RC_START_RC=0
    # No web-server is running unless a case sets a pid.
    export FAKE_WEBSERVER_PID=""
    reset_host

    write_fake_rc_common
    write_stock_app_service
    mock_pidof_webserver

    # The suite's bundles, rewritten into per-test copies that address MOCK_ROOT.
    patch_bundle "$UNINSTALL_BUNDLE" > "$BATS_TEST_TMPDIR/uninstall.sh"
    patch_bundle "$INSTALL_BUNDLE" > "$BATS_TEST_TMPDIR/install.sh"
    export UNINSTALL_BUNDLE="$BATS_TEST_TMPDIR/uninstall.sh"
    export INSTALL_BUNDLE="$BATS_TEST_TMPDIR/install.sh"

    # No path in REDIRECTED_HOST_PATHS may survive in either copy: one left
    # behind is a restore or uninstall step acting on this machine. Paths in
    # branches these cases never take (COSMOS, Snapmaker U1, the AD5M mods)
    # are not redirected, and in_bundle's rm refuses any deletion outside the
    # test's tmpdir.
    local b
    for b in "$UNINSTALL_BUNDLE" "$INSTALL_BUNDLE"; do
        refute grep -qE "(^|[^A-Za-z0-9_.-])($REDIRECTED_HOST_PATHS)" "$b"
        refute grep -qE '^main "\$@"$' "$b"
    done
}

teardown() {
    local p
    if [ -f "$BATS_TEST_TMPDIR/owned.pids" ]; then
        while read -r p; do
            [ -n "$p" ] && kill "$p" 2>/dev/null
        done < "$BATS_TEST_TMPDIR/owned.pids"
    fi
    return 0
}

# A bundle with its host paths redirected into MOCK_ROOT, its PATH hardening
# dropped so the mocked pidof resolves, and its entry dispatch removed so
# sourcing only defines functions.
patch_bundle() {
    sed -e "s|/etc/init\.d/|$MOCK_ROOT/etc/init.d/|g" \
        -e "s|/etc/rc\.d/|$MOCK_ROOT/etc/rc.d/|g" \
        -e "s|/etc/rc\.common|$MOCK_ROOT/etc/rc.common|g" \
        -e "s|/mnt/UDISK|$MOCK_ROOT/mnt/UDISK|g" \
        -e "s|/usr/bin/update-cosmos|$MOCK_ROOT/usr/bin/update-cosmos|g" \
        -e "s|/var/run/helix|$MOCK_ROOT/var/run/helix|g" \
        -e "s|/var/tmp/helix|$MOCK_ROOT/var/tmp/helix|g" \
        -e "s|\([ \"]\)/tmp/helix|\1$MOCK_ROOT/tmp/helix|g" \
        -e "s|/opt/\._helixscreen|$MOCK_ROOT/opt/._helixscreen|g" \
        -e "s|/root/\._helixscreen|$MOCK_ROOT/root/._helixscreen|g" \
        -e "s|/usr/sbin:/usr/bin:/sbin:/bin:||" \
        "$1" | sed -e '/^case "\${0##\*\/}" in$/,+2d' -e '/^main "\$@"$/d'
}

# Clear everything a run leaves on the mock host, for a case that runs twice.
reset_host() {
    : > "$EVENTS"
    rm -f "$WEB_KILLED" "$MOCK_ROOT"/etc/rc.d/*
}

# OpenWrt rc.common, reduced to what the restore and the ledger replay reach.
# `disable` removes links and nothing else, and `stop` runs the script's own
# stop(), so a stopped process is attributable to `stop` alone.
write_fake_rc_common() {
    cat > "$MOCK_ROOT/etc/rc.common" << 'RC_EOF'
#!/bin/sh
script="$1"
action="${2:-boot}"
name=$(basename "$script")
printf 'rc %s %s\n' "$name" "$action" >> "$EVENTS"
case "$action" in
    enable)
        for slot in $RC_ENABLE_LINKS; do
            ln -sfn "../init.d/$name" "$MOCK_ROOT/etc/rc.d/$slot$name"
        done
        ;;
    disable)
        rm -f "$MOCK_ROOT/etc/rc.d/S99$name" "$MOCK_ROOT/etc/rc.d/K01$name"
        ;;
    start)
        exit "$RC_START_RC"
        ;;
    stop)
        . "$script"
        # A USE_PROCD script has no stop() of its own; rc.common stops the
        # tracked instance (kills its pid, drops the record).
        if [ "$USE_PROCD" = 1 ]; then
            if [ -f "$MOCK_ROOT/var/run/procd-instances/$name" ]; then
                while IFS= read -r pf; do
                    _ip=$(cat "$pf" 2>/dev/null)
                    [ -n "$_ip" ] && kill "$_ip" 2>/dev/null
                done < "$MOCK_ROOT/var/run/procd-instances/$name"
                rm -f "$MOCK_ROOT/var/run/procd-instances/$name"
            fi
            exit 0
        fi
        stop
        ;;
esac
RC_EOF
    chmod +x "$MOCK_ROOT/etc/rc.common"
}

write_stock_app_service() {
    printf '#!/bin/sh %s/etc/rc.common\nSTART=99\nSTOP=01\n' "$MOCK_ROOT" \
        > "$MOCK_ROOT/etc/init.d/app"
    chmod +x "$MOCK_ROOT/etc/init.d/app"
}

# pidof answers for web-server only, and only until the kill shadow has
# signalled its pid.
mock_pidof_webserver() {
    mock_command_script "pidof" '
[ "$1" = web-server ] || exit 1
[ -n "$FAKE_WEBSERVER_PID" ] || exit 1
[ -f "$WEB_KILLED" ] && exit 1
echo "$FAKE_WEBSERVER_PID"'
}

# Source bundle $1 in /bin/sh and run the shell text $2 there.
#
# kill is a shell function in that shell, so `$SUDO kill <pid>` logs rather
# than signals. The first thing the shell does is prove that: a real kill of an
# unused pid fails and exits 97, the shadow logs "kill 2147483646". pidof is a
# function handing off to the PATH mock, because BusyBox ash runs its own pidof
# applet ahead of PATH. rm refuses any path outside the test's tmpdir, logging
# it as BLOCKED.
in_bundle() {
    sh -c '
        . "$1"
        SUDO=""
        AD5M_FIRMWARE=""
        PREVIOUS_UI_SCRIPT=""
        INIT_SYSTEM=sysv
        platform=k2
        pidof() { "$BATS_TEST_TMPDIR/bin/pidof" "$@"; }
        kill() {
            printf "kill %s\n" "$*" >> "$EVENTS"
            if [ -n "$FAKE_WEBSERVER_PID" ] && [ "$1" = "$FAKE_WEBSERVER_PID" ]; then
                : > "$WEB_KILLED"
            fi
            return 0
        }
        rm() {
            for _rm_arg in "$@"; do
                case "$_rm_arg" in
                    -*) ;;
                    "$BATS_TEST_TMPDIR"/*) ;;
                    *) printf "BLOCKED rm %s\n" "$*" >> "$EVENTS"; return 0 ;;
                esac
            done
            command rm "$@"
        }
        $SUDO kill 2147483646 || exit 97
        eval "$2"
    ' sh "$1" "$2"
}

# Run the restore and print what it published.
RESTORE_BODY='restore_previous_ui_platform k2
printf "CLAIM=[%s]\n" "$HELIX_RESTORED_UI"
printf "WARNED=[%s]\n" "${HELIX_RESTORE_WARNED:-}"'

# install.sh --uninstall's uninstall(), with every step before and after the
# restore stubbed to a no-op or pointed into the sandbox: the restore itself
# and the closing summary are the real code.
MODULE_UNINSTALL_BODY='
detect_init_system() { INIT_SYSTEM=sysv; }
remove_update_manager_section() { :; }
remove_moonraker_asvc() { :; }
find_moonraker_conf() { return 1; }
uninstall_camera_k2() { :; }
reenable_disabled_services() { :; }
remove_config_symlink() { :; }
clean_helix_state_dirs() { :; }
remove_legacy_moonraker_block() { :; }
helix_state_sweep_paths() { :; }
helix_state_prune_empty_roots() { :; }
HELIX_INIT_SCRIPTS=""
HELIX_PROCESSES=""
HELIX_INSTALL_DIRS="$INSTALL_DIR"
HELIX_STATE_DIRS=""
uninstall k2'

# The value of a NAME=[value] line in $output.
published() {
    printf '%s\n' "$output" | sed -n "s/^$1=\[\(.*\)\]\$/\1/p"
}

# The 1-based line of the first $EVENTS entry equal to $1, or fail.
event_line() {
    local n
    n=$(awk -v want="$1" '$0 == want { print NR; exit }' "$EVENTS")
    if [ -z "$n" ]; then
        printf 'not logged: %s\nevents:\n' "$1" >&2
        cat "$EVENTS" >&2
        return 1
    fi
    echo "$n"
}

# The run's shadow check passed, and nothing reached a host path through rm.
assert_sandboxed_run() {
    [ "$(sed -n 1p "$EVENTS")" = "kill 2147483646" ] \
        || fail "the kill shadow was not what ran: $(cat "$EVENTS")"
    refute_grep '^BLOCKED' "$EVENTS"
}

# $output after its last line containing $1.
output_after_last() {
    printf '%s\n' "$output" | awk -v mark="$1" '
        index($0, mark) { buf = ""; seen = 1; next }
        seen { buf = buf $0 "\n" }
        END { printf "%s", buf }'
}

# Kill of the live web-server logged before app start is dispatched.
assert_kill_precedes_start() {
    local k s
    k=$(event_line "kill $FAKE_WEBSERVER_PID")
    s=$(event_line "rc app start")
    [ "$k" -lt "$s" ] \
        || fail "web-server kill (event $k) must precede app start (event $s): $(cat "$EVENTS")"
}

# --- 1a/1b/1c: the boot link decides the claim ---

@test "k2 restore: S99app and K01app written: claims the S link, no warning, starts app" {
    local bundle claim
    for bundle in "$UNINSTALL_BUNDLE" "$INSTALL_BUNDLE"; do
        reset_host
        run in_bundle "$bundle" "$RESTORE_BODY"
        [ "$status" -eq 0 ] || fail "$bundle exited $status: $output"
        assert_sandboxed_run
        [ -L "$MOCK_ROOT/etc/rc.d/K01app" ] || fail "setup: enable did not write K01app"

        claim=$(published CLAIM)
        [ -n "$claim" ] || fail "no restore claimed with a valid S99app ($bundle): $output"
        contains "S99app" "$claim"
        lacks "K01app" "$claim"
        lacks "[WARN]" "$output"
        [ -z "$(published WARNED)" ] || fail "warned despite a valid boot link: $output"
        event_line "rc app start" >/dev/null
    done
}

@test "k2 restore: only K01app written: warns, claims nothing, still starts app" {
    export RC_ENABLE_LINKS="K01"
    local bundle
    for bundle in "$UNINSTALL_BUNDLE" "$INSTALL_BUNDLE"; do
        reset_host
        run in_bundle "$bundle" "$RESTORE_BODY"
        [ "$status" -eq 0 ] || fail "$bundle exited $status: $output"
        assert_sandboxed_run
        [ -L "$MOCK_ROOT/etc/rc.d/K01app" ] || fail "setup: enable did not write K01app"

        [ -z "$(published CLAIM)" ] \
            || fail "a shutdown link was claimed as a boot entry ($bundle): $output"
        contains "boot symlink missing or wrong" "$output"
        contains "/etc/init.d/app enable" "$(published WARNED)"
        event_line "rc app start" >/dev/null
    done
}

@test "k2 restore: no link written: warns, claims nothing, still starts app" {
    export RC_ENABLE_LINKS=""
    run in_bundle "$UNINSTALL_BUNDLE" "$RESTORE_BODY"
    [ "$status" -eq 0 ] || fail "exited $status: $output"
    assert_sandboxed_run

    [ -z "$(published CLAIM)" ] || fail "claimed a restore with no boot link: $output"
    contains "boot symlink missing or wrong" "$output"
    contains "/etc/init.d/app enable" "$(published WARNED)"
    event_line "rc app start" >/dev/null
}

# --- 1d: a live web-server is killed before app start ---

@test "k2 restore: live web-server is killed before app start (verified link)" {
    export FAKE_WEBSERVER_PID="$LIVE_WEBSERVER_PID"
    local bundle
    for bundle in "$UNINSTALL_BUNDLE" "$INSTALL_BUNDLE"; do
        reset_host
        run in_bundle "$bundle" "$RESTORE_BODY"
        [ "$status" -eq 0 ] || fail "$bundle exited $status: $output"
        assert_sandboxed_run
        [ -n "$(published CLAIM)" ] || fail "setup: this shape must verify the link: $output"
        assert_kill_precedes_start
    done
}

@test "k2 restore: live web-server is killed before app start (warn path)" {
    export FAKE_WEBSERVER_PID="$LIVE_WEBSERVER_PID"
    export RC_ENABLE_LINKS=""
    local bundle
    for bundle in "$UNINSTALL_BUNDLE" "$INSTALL_BUNDLE"; do
        reset_host
        run in_bundle "$bundle" "$RESTORE_BODY"
        [ "$status" -eq 0 ] || fail "$bundle exited $status: $output"
        assert_sandboxed_run
        contains "boot symlink missing or wrong" "$output"
        assert_kill_precedes_start
    done
}

# --- 1e: a failed app start says what the user lost ---

@test "k2 restore: a failed app start warns that Creality Cloud is down" {
    export RC_START_RC=1
    local bundle
    for bundle in "$UNINSTALL_BUNDLE" "$INSTALL_BUNDLE"; do
        reset_host
        run in_bundle "$bundle" "$RESTORE_BODY"
        [ "$status" -eq 0 ] || fail "$bundle exited $status: $output"
        assert_sandboxed_run
        event_line "rc app start" >/dev/null

        contains "[WARN]" "$output"
        contains "failed to start" "$output"
        contains "Creality Cloud" "$output"
        contains "port 80" "$output"
        contains "port 80" "$(published WARNED)"
        # The S link is verified in this shape, so a reboot does bring app up.
        contains "or reboot" "$(published WARNED)"
    done
}

# --- 1f: the callers' closing lines agree with the warning ---

@test "k2 restore warned: the standalone uninstaller's closing lines carry the fix" {
    export RC_ENABLE_LINKS=""
    run in_bundle "$UNINSTALL_BUNDLE" 'reenable_previous_ui'
    [ "$status" -eq 0 ] || fail "exited $status: $output"
    assert_sandboxed_run
    contains "boot symlink missing or wrong" "$output"

    local closing
    closing=$(output_after_last "boot symlink missing or wrong")
    lacks "No previous screen UI found" "$closing"
    lacks "a reboot may restore it" "$closing"
    contains "/etc/init.d/app enable" "$closing"
}

@test "k2 restore warned: install.sh --uninstall's closing lines carry the fix" {
    export RC_ENABLE_LINKS=""
    run in_bundle "$INSTALL_BUNDLE" "$MODULE_UNINSTALL_BODY"
    [ "$status" -eq 0 ] || fail "exited $status: $output"
    assert_sandboxed_run
    contains "HelixScreen uninstalled" "$output"
    contains "boot symlink missing or wrong" "$output"

    local closing
    closing=$(output_after_last "boot symlink missing or wrong")
    contains "HelixScreen uninstalled" "$closing"
    lacks "No previous UI found to restore" "$closing"
    contains "/etc/init.d/app enable" "$closing"
}

# The caller's "restore incomplete" summary names both manual fixes, and the
# run offers a reboot nowhere.
assert_both_fixes_without_reboot() {
    local summary
    summary=$(printf '%s\n' "$output" | grep 'Previous UI restore incomplete') \
        || fail "no restore-incomplete summary: $output"
    contains "/etc/init.d/app enable" "$summary"
    contains "/etc/init.d/app start" "$summary"
    lacks "or reboot" "$output"
}

@test "k2 restore: no S link and a failed start: both callers give both fixes and no reboot" {
    # Without an S link a reboot starts nothing, so only the two commands help.
    export RC_ENABLE_LINKS=""
    export RC_START_RC=1

    run in_bundle "$UNINSTALL_BUNDLE" 'reenable_previous_ui'
    [ "$status" -eq 0 ] || fail "standalone exited $status: $output"
    assert_sandboxed_run
    contains "boot symlink missing or wrong" "$output"
    contains "failed to start" "$output"
    assert_both_fixes_without_reboot

    reset_host
    run in_bundle "$INSTALL_BUNDLE" "$MODULE_UNINSTALL_BODY"
    [ "$status" -eq 0 ] || fail "install.sh --uninstall exited $status: $output"
    assert_sandboxed_run
    contains "boot symlink missing or wrong" "$output"
    contains "failed to start" "$output"
    assert_both_fixes_without_reboot
}

@test "k2 restore: a second call publishes its own warning state, not the first call's" {
    local bundle
    for bundle in "$UNINSTALL_BUNDLE" "$INSTALL_BUNDLE"; do
        reset_host
        run in_bundle "$bundle" '
            export RC_ENABLE_LINKS=""
            restore_previous_ui_platform k2
            printf "W1=[%s]\n" "$HELIX_RESTORE_WARNED"
            export RC_ENABLE_LINKS="S99 K01"
            restore_previous_ui_platform k2
            printf "W2=[%s]\n" "$HELIX_RESTORE_WARNED"
            printf "C2=[%s]\n" "$HELIX_RESTORED_UI"'
        [ "$status" -eq 0 ] || fail "$bundle exited $status: $output"
        assert_sandboxed_run
        [ -n "$(published W1)" ] || fail "setup: the first call must warn ($bundle): $output"
        [ -n "$(published C2)" ] || fail "setup: the second call must verify the link ($bundle): $output"
        [ -z "$(published W2)" ] \
            || fail "the first call's warning survived a clean second call ($bundle): $output"
    done
}

# --- the verified slot comes from the script's own directives ---

@test "k2 restore: a stock app declaring START=50 verifies at its own slot" {
    # The expected S slot is derived from the stock script's own START, so a
    # firmware variant numbering its boot order differently still verifies.
    printf '#!/bin/sh %s/etc/rc.common\nSTART=50\nSTOP=01\n' "$MOCK_ROOT" \
        > "$MOCK_ROOT/etc/init.d/app"
    chmod +x "$MOCK_ROOT/etc/init.d/app"
    export RC_ENABLE_LINKS="S50 K01"
    run in_bundle "$UNINSTALL_BUNDLE" "$RESTORE_BODY"
    [ "$status" -eq 0 ] || fail "exited $status: $output"
    assert_sandboxed_run
    [ -n "$(published CLAIM)" ] || fail "a declared-slot entry went unverified: $output"
    contains "S50app" "$(published CLAIM)"
    [ -z "$(published WARNED)" ] || fail "warned despite the declared slot verifying: $output"
}

@test "k2 restore: a stale different-slot entry is dropped for the declared slot" {
    # A pre-existing rc.d entry in another slot must not survive beside the
    # fresh pair: the enable path drops every S??app/K??app first, so the
    # boot entry ends up exactly where the stock script declares it.
    ln -s ../init.d/app "$MOCK_ROOT/etc/rc.d/S50app"
    run in_bundle "$UNINSTALL_BUNDLE" "$RESTORE_BODY"
    [ "$status" -eq 0 ] || fail "exited $status: $output"
    assert_sandboxed_run
    [ ! -e "$MOCK_ROOT/etc/rc.d/S50app" ] || fail "stale S50app survived the restore"
    [ -L "$MOCK_ROOT/etc/rc.d/S99app" ] || fail "the declared-slot entry is missing"
    contains "S99app" "$(published CLAIM)"
}

@test "k2 restore: a stock app without STOP restores S-verified with a warn" {
    # STOP is verified-when-declared: a stock firmware script we do not
    # author must restore fine, its boot entry verified on the S half.
    printf '#!/bin/sh %s/etc/rc.common\nSTART=99\n' "$MOCK_ROOT" \
        > "$MOCK_ROOT/etc/init.d/app"
    chmod +x "$MOCK_ROOT/etc/init.d/app"
    export RC_ENABLE_LINKS="S99"
    run in_bundle "$UNINSTALL_BUNDLE" "$RESTORE_BODY"
    [ "$status" -eq 0 ] || fail "exited $status: $output"
    assert_sandboxed_run
    contains "S99app" "$(published CLAIM)"
    # The helper warned at its own level; the restore itself claims success.
    [ -z "$(published WARNED)" ] || fail "restore warned at its own level: $output"
    contains "no parseable STOP" "$output"
    event_line "rc app start" >/dev/null
}

# --- 1g: the sysv-created ledger replay stops what the carve-out runs ---

@test "k2 uninstall: sysv-created replay stops the running web-server and removes script and links" {
    local bundle init pidfile pid record
    init="$MOCK_ROOT/etc/init.d/helix-k2-webserver"
    pidfile="$MOCK_ROOT/var/run/procd-helix-k2-webserver.pid"
    record="$MOCK_ROOT/var/run/procd-instances/helix-k2-webserver"
    for bundle in "$UNINSTALL_BUNDLE" "$INSTALL_BUNDLE"; do
        reset_host
        sed -e "s|#!/bin/sh /etc/rc.common|#!/bin/sh $MOCK_ROOT/etc/rc.common|" \
            -e "s|/usr/sbin:/usr/bin:/sbin:/bin:||" \
            -e "s|/usr/bin/web-server|$MOCK_ROOT/usr/bin/web-server|g" \
            "$WORKTREE_ROOT/config/k2-webserver.init" > "$init"
        chmod +x "$init"
        "$init" enable
        [ -L "$MOCK_ROOT/etc/rc.d/S99helix-k2-webserver" ]
        [ -L "$MOCK_ROOT/etc/rc.d/K01helix-k2-webserver" ]

        # The running carve-out: a process this test owns, orphaned so it is
        # reaped as soon as it dies, with fd 3 closed so bats does not wait
        # on it. The procd model tracks it as the registered instance.
        ( sleep 300 > /dev/null 2>&1 3>&- & echo $! > "$pidfile" )
        pid=$(cat "$pidfile")
        mkdir -p "$MOCK_ROOT/var/run/procd-instances"
        echo "$pidfile" > "$record"
        echo "$pid" >> "$BATS_TEST_TMPDIR/owned.pids"
        kill -0 "$pid"

        echo "sysv-created:$init" > "$INSTALL_DIR/config/.disabled_services"
        : > "$EVENTS"
        run in_bundle "$bundle" 'reenable_disabled_services'
        [ "$status" -eq 0 ] || fail "$bundle exited $status: $output"
        assert_sandboxed_run

        local tries=0
        while kill -0 "$pid" 2>/dev/null; do
            tries=$((tries + 1))
            [ "$tries" -lt 50 ] || fail "replay left the carve-out web-server $pid running ($bundle): $(cat "$EVENTS")"
            sleep 0.1
        done
        [ ! -e "$init" ] || fail "replay left $init in place"
        [ ! -e "$MOCK_ROOT/etc/rc.d/S99helix-k2-webserver" ] || fail "S99 link survived"
        [ ! -e "$MOCK_ROOT/etc/rc.d/K01helix-k2-webserver" ] || fail "K01 link survived"
    done
}

# --- 1h: the kill the restore depends on exists where the restore runs ---

@test "k2 restore: kill_process_by_name is defined in both bundles and reached by the restore" {
    export FAKE_WEBSERVER_PID="$LIVE_WEBSERVER_PID"
    local bundle entry
    for bundle in "$UNINSTALL_BUNDLE" "$INSTALL_BUNDLE"; do
        reset_host
        # The standalone uninstaller reaches the restore through
        # reenable_previous_ui; install.sh reaches it through uninstall().
        entry='restore_previous_ui_platform k2'
        [ "$bundle" = "$UNINSTALL_BUNDLE" ] && entry='reenable_previous_ui'
        # type words its answer differently in dash, bash and BusyBox ash, but
        # each says "function" for a defined one and fails for a missing one.
        run in_bundle "$bundle" "type kill_process_by_name | grep -q function || exit 96
$entry"
        [ "$status" -eq 0 ] || fail "$bundle exited $status: $output"
        assert_sandboxed_run
        lacks "not found" "$output"
        assert_kill_precedes_start
    done
}
