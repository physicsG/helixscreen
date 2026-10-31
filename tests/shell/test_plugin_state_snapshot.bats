#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/plugin-state-snapshot.sh: argument parsing, path resolution against
# fixture printers (--root reads a directory tree as the printer's "/"), the
# read-only contract, and --diff. Nothing here opens an ssh connection; ssh and
# sshpass are shadowed by stubs that log and fail.

load helpers

REPO_ROOT="$(cd "${BATS_TEST_DIRNAME}/../.." && pwd)"
SNAP="$REPO_ROOT/scripts/plugin-state-snapshot.sh"

setup() {
    OUT="$BATS_TEST_TMPDIR/out"
    mock_command_script ssh "printf '%s\\n' \"\$*\" >> '$BATS_TEST_TMPDIR/ssh.log'; exit 255"
}

# A process under <root>/proc: argv, owner uid, a stat whose comm holds a
# space and a ')', and the chroot it runs in (a printer path, "/" for none).
fake_proc() { # <root> <pid> <uid> <start ticks> <chroot> <argv...>
    local root=$1 pid=$2 uid=$3 start=$4 chroot=$5
    shift 5
    local d="$root/proc/$pid"
    mkdir -p "$d"
    printf '%s\0' "$@" > "$d/cmdline"
    printf 'Name:\tx\nUid:\t%s\t%s\t%s\t%s\n' "$uid" "$uid" "$uid" "$uid" > "$d/status"
    # Fields 3..21 are filler; field 22 is the start time.
    printf '%s (py (x) y) S 1 1 1 0 -1 0 0 0 0 0 0 0 0 0 20 0 1 0 %s 0 0\n' "$pid" "$start" > "$d/stat"
    ln -sfn "$chroot" "$d/root"
}

write_passwd() { # <root> <line...>
    local root=$1
    shift
    mkdir -p "$root/etc"
    printf '%s\n' "$@" > "$root/etc/passwd"
}

# The app: helix-screen run from <install>/bin by <uid>.
fake_app() { # <root> <pid> <uid> <install dir> <version>
    local root=$1 pid=$2 uid=$3 inst=$4 ver=$5
    fake_proc "$root" "$pid" "$uid" 5000 / "$inst/bin/helix-screen" -v
    ln -sfn "$inst/bin/helix-screen" "$root/proc/$pid/exe"
    mkdir -p "$root$inst/moonraker-plugin"
    : > "$root$inst/moonraker-plugin/helix_print.py"
    printf '{"project_name": "helixscreen", "version": "%s"}\n' "$ver" > "$root$inst/release_info.json"
}

# A Pi: everything under /home/pi, a git-checkout Moonraker, a moonraker.conf
# whose [include] glob reaches a file that includes another, and the plugin
# installed by symlink.
setup_pi() {
    local r=$1
    write_passwd "$r" 'root:x:0:0:root:/root:/bin/sh' 'pi:x:1000:1000::/home/pi:/bin/bash'
    mkdir -p "$r/home/pi/moonraker/moonraker/components" "$r/home/pi/printer_data/config/extra" \
        "$r/home/pi/printer_data/gcodes/.helix_temp" "$r/run/systemd/system"
    : > "$r/home/pi/moonraker/moonraker/moonraker.py"
    printf '[server]\nhost: 0.0.0.0\n\n[include extra/*.conf]\n' > "$r/home/pi/printer_data/config/moonraker.conf"
    printf '[include nested.cfg]\n[power printer]\n' > "$r/home/pi/printer_data/config/extra/power.conf"
    printf '[update_manager x]\n' > "$r/home/pi/printer_data/config/extra/nested.cfg"
    printf '[printer]\n' > "$r/home/pi/printer_data/config/printer.cfg"
    : > "$r/home/pi/printer_data/config/moonraker.conf.bak.20260101_000000"
    fake_proc "$r" 812 1000 41000 / /home/pi/moonraker-env/bin/python \
        /home/pi/moonraker/moonraker/moonraker.py -d /home/pi/printer_data
    fake_app "$r" 900 1000 /home/pi/helixscreen 1.2.3
    ln -s /home/pi/helixscreen/moonraker-plugin/helix_print.py \
        "$r/home/pi/moonraker/moonraker/components/helix_print.py"
}

# A Creality K1: root everywhere, nothing under /root, the Moonraker repo nested
# in /usr/data/moonraker, so only the running process says where anything is.
setup_k1() {
    local r=$1
    write_passwd "$r" 'root:x:0:0:root:/root:/bin/sh'
    local pkg="$r/usr/data/moonraker/moonraker/moonraker"
    mkdir -p "$pkg/components" "$r/usr/data/printer_data/config" "$r/root"
    : > "$pkg/moonraker.py"
    printf '[server]\nport: 7125\n' > "$r/usr/data/printer_data/config/moonraker.conf"
    fake_proc "$r" 1444 0 7700 / /usr/data/moonraker/moonraker-env/bin/python \
        /usr/data/moonraker/moonraker/moonraker/moonraker.py -d /usr/data/printer_data
    fake_app "$r" 1500 0 /usr/data/helixscreen 1.0.4
}

# Forge-X: Moonraker in a chroot at /data/.mod/.forge-x, its moonraker.conf a
# symlink into the mod's tree that includes mod_data/user.moonraker.conf.
FX=/data/.mod/.forge-x
setup_forgex() {
    local r=$1
    write_passwd "$r" 'root:x:0:0:root:/root:/bin/sh'
    local c="$r$FX"
    mkdir -p "$c/root/moonraker-env/moonraker/components" "$c/root/printer_data/config/mod" \
        "$c/root/printer_data/config/mod_data"
    : > "$c/root/moonraker-env/moonraker/moonraker.py"
    printf '[server]\nport: 7125\n\n[include mod_data/user.moonraker.conf]\n' \
        > "$c/root/printer_data/config/mod/moonraker.conf"
    ln -s mod/moonraker.conf "$c/root/printer_data/config/moonraker.conf"
    printf '[authorization]\n\n[helix_print]\n' > "$c/root/printer_data/config/mod_data/user.moonraker.conf"
    fake_proc "$r" 1925 0 3100 "$FX" /root/moonraker-env/bin/python3 \
        /root/moonraker-env/moonraker/moonraker.py -d /root/printer_data
    fake_app "$r" 2000 0 /opt/helixscreen 1.0.4
}

# A mod that owns moonraker.conf on a plain filesystem: the conf is a symlink
# into the mod, the user's sections live in user.moonraker.conf beside it.
setup_modconf() {
    local r=$1
    setup_pi "$r"
    local cfg="$r/home/pi/printer_data/config"
    mkdir -p "$cfg/mod"
    printf '[server]\n\n[include user.moonraker.conf]\n' > "$cfg/mod/moonraker.conf"
    rm "$cfg/moonraker.conf"
    ln -s mod/moonraker.conf "$cfg/moonraker.conf"
    printf '[helix_print]\n' > "$cfg/user.moonraker.conf"
}

snap() { # <root> <label> [options...]
    local root=$1 label=$2
    shift 2
    run "$SNAP" --root "$root" --no-http --out "$OUT" "$@" dev "$label"
}

snap_dir() { # <label>
    local d
    for d in "$OUT"/dev/plugin-test/*-"$1"; do printf '%s' "$d"; return 0; done
}

path_of() { # <snapshot dir> <key>
    sed -n "s/^$2=//p" "$1/paths.txt"
}

# --- arguments ---------------------------------------------------------------

@test "no arguments prints usage and exits 2" {
    run "$SNAP"
    [ "$status" -eq 2 ]
    contains "--ssh USER@HOST" "$output"
}

@test "an unknown option is refused" {
    run "$SNAP" --password hunter2 --ssh pi@h dev x
    [ "$status" -eq 2 ]
    contains "unknown option --password" "$output"
}

@test "a non-numeric port is refused" {
    run "$SNAP" --ssh pi@h --port eighty dev x
    [ "$status" -eq 2 ]
    contains "--port takes a number" "$output"
}

@test "a relative chroot is refused" {
    run "$SNAP" --ssh pi@h --chroot data/.mod dev x
    [ "$status" -eq 2 ]
    contains "absolute path" "$output"
}

@test "device and label must be plain names" {
    run "$SNAP" --ssh pi@h ../up x
    [ "$status" -eq 2 ]
    run "$SNAP" --ssh pi@h dev 'a b'
    [ "$status" -eq 2 ]
    run "$SNAP" --ssh pi@h dev
    [ "$status" -eq 2 ]
}

@test "a snapshot needs --ssh or --root, never both" {
    run "$SNAP" dev x
    [ "$status" -eq 2 ]
    contains "--ssh USER@HOST is required" "$output"
    mkdir -p "$BATS_TEST_TMPDIR/r"
    run "$SNAP" --root "$BATS_TEST_TMPDIR/r" --ssh pi@h dev x
    [ "$status" -eq 2 ]
}

@test "--sshpass-env takes a variable name that is set" {
    mock_command_script sshpass 'exit 1'
    run "$SNAP" --ssh pi@h --sshpass-env 'not a name' dev x
    [ "$status" -eq 2 ]
    contains "variable name" "$output"
    unset SNAP_TEST_UNSET_PW
    run "$SNAP" --ssh pi@h --sshpass-env SNAP_TEST_UNSET_PW dev x
    [ "$status" -eq 2 ]
    contains "is not set" "$output"
}

@test "the password reaches sshpass through the environment, never argv" {
    local log="$BATS_TEST_TMPDIR/sshpass.log"
    mock_command_script sshpass "printf 'argv=%s\\nenv=%s\\n' \"\$*\" \"\$SSHPASS\" >> '$log'; cat >/dev/null; exit 1"
    SNAP_TEST_PW='s3cret-pw' run "$SNAP" --ssh root@h --sshpass-env SNAP_TEST_PW --no-http \
        --out "$OUT" dev x
    [ "$status" -ne 0 ]
    grep -q '^env=s3cret-pw$' "$log" || fail "sshpass did not get SSHPASS: $(cat "$log")"
    grep '^argv=' "$log" | grep -q -- '-e ssh' || fail "sshpass not run with -e: $(cat "$log")"
    ! grep '^argv=' "$log" | grep -q 's3cret-pw' || fail "the password is on a command line"
}

@test "--diff takes exactly two snapshot directories" {
    run "$SNAP" --diff "$BATS_TEST_TMPDIR"
    [ "$status" -eq 2 ]
    mkdir -p "$BATS_TEST_TMPDIR/a" "$BATS_TEST_TMPDIR/b"
    run "$SNAP" --diff "$BATS_TEST_TMPDIR/a" "$BATS_TEST_TMPDIR/b"
    [ "$status" -eq 2 ]
    contains "not a snapshot" "$output"
}

# --- the printer-side script ---------------------------------------------------

@test "the printer runs install.sh's own discovery functions, verbatim" {
    run "$SNAP" --emit-remote
    [ "$status" -eq 0 ]
    local fn body
    for fn in components_under find_moonraker_components find_config_dir helix_conf_file; do
        body=$(sed -n "/^$fn() {\$/,/^}\$/p" "$REPO_ROOT/moonraker-plugin/install.sh")
        [ -n "$body" ] || fail "no $fn in install.sh"
        contains "$body" "$output"
    done
}

@test "the printer-side script is POSIX sh" {
    "$SNAP" --emit-remote > "$BATS_TEST_TMPDIR/remote.sh"
    sh -n "$BATS_TEST_TMPDIR/remote.sh"
    if command -v shellcheck >/dev/null 2>&1; then
        shellcheck -s sh "$BATS_TEST_TMPDIR/remote.sh"
    fi
}

@test "an install.sh missing a discovery function fails the snapshot" {
    sed '/^helix_conf_file() {$/,/^}$/d' "$REPO_ROOT/moonraker-plugin/install.sh" > "$BATS_TEST_TMPDIR/install.sh"
    cp "$REPO_ROOT/moonraker-plugin/helix_print_conf.sh" "$BATS_TEST_TMPDIR/"
    setup_pi "$BATS_TEST_TMPDIR/pi"
    HELIX_PLUGIN_INSTALL_SH="$BATS_TEST_TMPDIR/install.sh" snap "$BATS_TEST_TMPDIR/pi" p0
    [ "$status" -ne 0 ]
    contains "lacks helix_conf_file" "$output"
}

# --- path resolution -------------------------------------------------------------

@test "a Pi resolves its home Moonraker, every include and both users" {
    local r="$BATS_TEST_TMPDIR/pi"
    setup_pi "$r"
    snap "$r" p0
    [ "$status" -eq 0 ] || fail "$output"
    local d
    d=$(snap_dir p0)
    [ "$(path_of "$d" components_dir)" = /home/pi/moonraker/moonraker/components ]
    [ "$(path_of "$d" config_dir)" = /home/pi/printer_data/config ]
    [ "$(path_of "$d" section_file)" = /home/pi/printer_data/config/moonraker.conf ]
    [ "$(path_of "$d" moonraker_user)" = pi ]
    [ "$(path_of "$d" app_user)" = pi ]
    [ "$(path_of "$d" install_dir)" = /home/pi/helixscreen ]
    [ "$(path_of "$d" helixscreen_version)" = 1.2.3 ]
    [ "$(path_of "$d" moonraker_start_ticks)" = 41000 ]
    [ "$(path_of "$d" init)" = systemd ]
    [ "$(path_of "$d" plugin_link_state)" = symlink ]
    [ "$(path_of "$d" plugin_link_resolves)" = yes ]
    grep -qx 'include=/home/pi/printer_data/config/extra/power.conf' "$d/paths.txt"
    grep -qx 'include=/home/pi/printer_data/config/extra/nested.cfg' "$d/paths.txt"
    [ -f "$d/files/home/pi/printer_data/config/extra/nested.cfg" ]
    [ -f "$d/files/home/pi/printer_data/config/printer.cfg" ]
    [ -f "$d/files/home/pi/helixscreen/release_info.json" ]
    contains 'helix_print.py -> /home/pi/helixscreen/moonraker-plugin/helix_print.py' "$(cat "$d/components_ls.txt")"
    grep -q '^.helix_temp: ' "$d/gcodes_ls.txt"
    grep -qx '/home/pi/printer_data/config/moonraker.conf.bak.20260101_000000' "$d/backups.txt"
    grep -q ' /home/pi/printer_data/config/moonraker.conf$' "$d/checksums.txt"
}

@test "the snapshot walks includes with the installer's own walker, so a cycle lists each file once" {
    run "$SNAP" --emit-remote
    [ "$status" -eq 0 ]
    local body
    body=$(sed -n '/^moonraker_conf_files() {$/,/^}$/p' "$REPO_ROOT/moonraker-plugin/helix_print_conf.sh")
    [ -n "$body" ] || fail "no moonraker_conf_files in helix_print_conf.sh"
    contains "$body" "$output"

    local r="$BATS_TEST_TMPDIR/pi"
    setup_pi "$r"
    printf '[include ../moonraker.conf]\n' >> "$r/home/pi/printer_data/config/extra/nested.cfg"
    snap "$r" p0
    [ "$status" -eq 0 ] || fail "$output"
    [ "$(grep -c '^include=' "$(snap_dir p0)/paths.txt")" -eq 2 ] ||
        fail "includes: $(grep '^include=' "$(snap_dir p0)/paths.txt" | head -n 4)"
}

@test "a dangling plugin link is reported as not resolving" {
    local r="$BATS_TEST_TMPDIR/pi"
    setup_pi "$r"
    rm "$r/home/pi/helixscreen/moonraker-plugin/helix_print.py"
    snap "$r" p0
    [ "$status" -eq 0 ] || fail "$output"
    [ "$(path_of "$(snap_dir p0)" plugin_link_resolves)" = no ]
}

@test "a K1 resolves its nested Moonraker from the running process" {
    local r="$BATS_TEST_TMPDIR/k1"
    setup_k1 "$r"
    snap "$r" p0
    [ "$status" -eq 0 ] || fail "$output"
    local d
    d=$(snap_dir p0)
    [ "$(path_of "$d" components_dir)" = /usr/data/moonraker/moonraker/moonraker/components ]
    [ "$(path_of "$d" config_dir)" = /usr/data/printer_data/config ]
    [ "$(path_of "$d" moonraker_user)" = root ]
    [ "$(path_of "$d" app_home)" = /root ]
    [ "$(path_of "$d" plugin_link_state)" = absent ]
    [ "$(path_of "$d" init)" = sysv ]
    [ -f "$d/files/usr/data/printer_data/config/moonraker.conf" ]
}

@test "Forge-X resolves inside the chroot and keeps the mod's conf symlink" {
    local r="$BATS_TEST_TMPDIR/fx"
    setup_forgex "$r"
    snap "$r" p0
    [ "$status" -eq 0 ] || fail "$output"
    local d cfg="$FX/root/printer_data/config"
    d=$(snap_dir p0)
    [ "$(path_of "$d" moonraker_root)" = "$FX" ]
    [ "$(path_of "$d" components_dir)" = "$FX/root/moonraker-env/moonraker/components" ]
    [ "$(path_of "$d" config_dir)" = "$cfg" ]
    [ "$(path_of "$d" section_file)" = "$cfg/mod_data/user.moonraker.conf" ]
    [ "$(path_of "$d" moonraker_conf_is_symlink)" = yes ]
    [ "$(path_of "$d" moonraker_conf_real)" = "$cfg/mod/moonraker.conf" ]
    grep -qx "include=$cfg/mod_data/user.moonraker.conf" "$d/paths.txt"
    [ -L "$d/files$cfg/moonraker.conf" ] || fail "moonraker.conf was not copied as a symlink"
    [ -f "$d/files$cfg/mod/moonraker.conf" ]
    [ -f "$d/files$cfg/mod_data/user.moonraker.conf" ]
}

@test "--chroot names the chroot when /proc/<pid>/root cannot be read" {
    local r="$BATS_TEST_TMPDIR/fx"
    setup_forgex "$r"
    rm "$r/proc/1925/root"
    snap "$r" blind
    [ "$status" -eq 0 ] || fail "$output"
    [ -z "$(path_of "$(snap_dir blind)" components_dir)" ]
    snap "$r" named --chroot "$FX"
    [ "$status" -eq 0 ] || fail "$output"
    [ "$(path_of "$(snap_dir named)" components_dir)" = "$FX/root/moonraker-env/moonraker/components" ]
}

@test "a mod-owned symlinked conf puts the section in user.moonraker.conf" {
    local r="$BATS_TEST_TMPDIR/mod"
    setup_modconf "$r"
    snap "$r" p0
    [ "$status" -eq 0 ] || fail "$output"
    local d cfg=/home/pi/printer_data/config
    d=$(snap_dir p0)
    [ "$(path_of "$d" section_file)" = "$cfg/user.moonraker.conf" ]
    [ "$(path_of "$d" moonraker_conf_real)" = "$cfg/mod/moonraker.conf" ]
    [ -f "$d/files$cfg/user.moonraker.conf" ]
}

@test "a snapshot writes nothing on the printer" {
    local r="$BATS_TEST_TMPDIR/fx"
    setup_forgex "$r"
    setup_pi "$BATS_TEST_TMPDIR/pi"
    local root before after
    for root in "$r" "$BATS_TEST_TMPDIR/pi"; do
        before=$(cd "$root" && find . -printf '%p %y %s %m %T@ %l\n' | sort)
        snap "$root" ro
        [ "$status" -eq 0 ] || fail "$output"
        after=$(cd "$root" && find . -printf '%p %y %s %m %T@ %l\n' | sort)
        [ "$before" = "$after" ] || fail "the tree changed: $(diff <(echo "$before") <(echo "$after"))"
        rm -rf "$OUT"
    done
    [ ! -e "$BATS_TEST_TMPDIR/ssh.log" ] || fail "ssh was called: $(cat "$BATS_TEST_TMPDIR/ssh.log")"
}

# --- the HTTP half -----------------------------------------------------------------

@test "the summary carries components, failures, warnings and the print state" {
    local r="$BATS_TEST_TMPDIR/pi" web="$BATS_TEST_TMPDIR/web"
    setup_pi "$r"
    mkdir -p "$web"
    printf '%s' '{"result":{"klippy_state":"ready","components":["webcams","helix_print"],"failed_components":["helix_print"],"warnings":["Unparsed config section [helix_print]"],"moonraker_version":"v0.9.3","api_version_string":"1.5.0"}}' > "$web/server_info"
    printf '%s' '{"result":{"status":{"print_stats":{"state":"standby"}}}}' > "$web/print_stats"
    mock_command_script curl "for a; do u=\$a; done
case \"\$u\" in
    */server/info) cat '$web/server_info' ;;
    */printer/objects/query*) cat '$web/print_stats' ;;
    *) printf '{\"error\": {\"code\": 404}}' ;;
esac"
    run "$SNAP" --root "$r" --http printer.invalid --out "$OUT" dev p0
    [ "$status" -eq 0 ] || fail "$output"
    local s
    s=$(cat "$(snap_dir p0)/summary.txt")
    contains "components=helix_print webcams" "$s"
    contains "failed_components=helix_print" "$s"
    contains "warning=Unparsed config section [helix_print]" "$s"
    contains "helix_status=absent" "$s"
    contains "print_state=standby" "$s"
}

# --- diff ----------------------------------------------------------------------------

@test "two snapshots of an unchanged printer do not differ" {
    local r="$BATS_TEST_TMPDIR/pi"
    setup_pi "$r"
    snap "$r" a
    snap "$r" b
    run "$SNAP" --diff "$(snap_dir a)" "$(snap_dir b)"
    [ "$status" -eq 0 ] || fail "$output"
    contains "no differences" "$output"
}

@test "a Moonraker restart is reported and is not a difference" {
    local r="$BATS_TEST_TMPDIR/pi"
    setup_pi "$r"
    snap "$r" a
    mv "$r/proc/812" "$r/proc/813"
    sed -i 's/^812 /813 /; s/ 41000 / 52000 /' "$r/proc/813/stat"
    snap "$r" b
    run "$SNAP" --diff "$(snap_dir a)" "$(snap_dir b)"
    [ "$status" -eq 0 ] || fail "$output"
    contains "restart: moonraker_pid 812 -> 813" "$output"
    contains "restart: moonraker_start_ticks 41000 -> 52000" "$output"
}

@test "an edited conf and a new plugin link are differences" {
    local r="$BATS_TEST_TMPDIR/k1"
    setup_k1 "$r"
    snap "$r" a
    printf '\n[helix_print]\n' >> "$r/usr/data/printer_data/config/moonraker.conf"
    ln -s /usr/data/helixscreen/moonraker-plugin/helix_print.py \
        "$r/usr/data/moonraker/moonraker/moonraker/components/helix_print.py"
    snap "$r" b
    run "$SNAP" --diff "$(snap_dir a)" "$(snap_dir b)"
    [ "$status" -eq 1 ] || fail "status $status: $output"
    contains "+plugin_link_state=symlink" "$output"
    contains "=== checksums.txt" "$output"
    contains "+[helix_print]" "$output"
}
