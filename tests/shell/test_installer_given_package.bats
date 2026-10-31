#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# A package handed to the installer is the package it installs: an archive
# named on the command line (with or without --local), or the extracted
# release tree the installer itself was unpacked with. Neither consults the
# channel or the network. --version and --update still resolve online, and a
# version with no build for this platform names the release that has one.
#
# The whole-install cases run main() as an x86 desktop box: that flavour
# installs into the user's home, so every path it writes is the test's own.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"
LIB="$WORKTREE_ROOT/scripts/lib/installer"

setup() {
    load helpers
    STUBBIN="$BATS_TEST_TMPDIR/stubbin"
    mkdir -p "$STUBBIN"
    for c in sudo apt-get apt apt-cache dpkg-query pkexec udevadm pidof curl wget; do
        printf '#!/bin/sh\necho "%s $*" >> "%s/calls.log"\nexit 1\n' "$c" "$BATS_TEST_TMPDIR" > "$STUBBIN/$c"
        chmod +x "$STUBBIN/$c"
    done
    PATH="$STUBBIN:$PATH"
    mock_command_script systemctl "echo \"systemctl \$*\" >> '$BATS_TEST_TMPDIR/calls.log'; exit 1"
    mock_command_script id "[ \"\$1\" = -u ] && [ \$# -eq 1 ] && { echo 1000; exit 0; }
exec /usr/bin/id \"\$@\""

    export HOME="$BATS_TEST_TMPDIR/home/alice"
    mkdir -p "$HOME"
    unset XDG_DATA_HOME XDG_STATE_HOME KLIPPER_HOME DISPLAY WAYLAND_DISPLAY XDG_SESSION_TYPE INSTALL_DIR TMP_DIR
    unset HELIX_SELF_UPDATE R2_CHANNEL
    export HELIX_SYSTEMD_UNIT_DIR="$BATS_TEST_TMPDIR/etc/systemd/system"
    export HELIX_INIT_D_DIR="$BATS_TEST_TMPDIR/etc/init.d"
    export HELIX_HOME_ROOT="$BATS_TEST_TMPDIR/home"
    export HELIX_PROC_ROOT="$BATS_TEST_TMPDIR/no-proc"
    export _HELIX_KNOWN_INSTALL_DIRS="$BATS_TEST_TMPDIR/opt/helixscreen"
    export HELIX_TTY_DEVICE=/nonexistent
    export HELIX_INSTALL_TTY=0
    export SUDO=""
    mkdir -p "$HELIX_SYSTEMD_UNIT_DIR" "$HELIX_INIT_D_DIR"

    local bats_traps
    bats_traps="$(trap -p EXIT INT TERM HUP)"
    for m in common logo host_profile platform permissions requirements competing_uis desktop release service moonraker kiauh printer_seed plan uninstall main; do
        . "$LIB/$m.sh"
    done
    trap - ERR EXIT INT TERM HUP
    eval "$bats_traps"

    DESK="$HOME/.local/share/helixscreen"
    DL="$HOME/Downloads"
    mkdir -p "$DL"

    # Every way the installer reaches the network records itself and fails,
    # so a test can prove a run never asked.
    get_latest_version() { echo "NETWORK get_latest_version $*" >> "$BATS_TEST_TMPDIR/calls.log"; echo v9.9.9; }
    _url_probe() { echo "NETWORK probe $*" >> "$BATS_TEST_TMPDIR/calls.log"; echo unknown; }
    _fetch_channel_manifest() { echo "NETWORK manifest $*" >> "$BATS_TEST_TMPDIR/calls.log"; return 1; }
    download_release() { echo "NETWORK download $*" >> "$BATS_TEST_TMPDIR/calls.log"; exit 1; }
}

called() { grep -qs "^$1" "$BATS_TEST_TMPDIR/calls.log"; }
# A request leaves the machine; `curl --version` only asks what curl this is.
reached_out() { grep -v '^curl --version$' "$BATS_TEST_TMPDIR/calls.log" 2>/dev/null | grep -qE '^(NETWORK|curl|wget) '; }
calls() { cat "$BATS_TEST_TMPDIR/calls.log" 2>/dev/null; }

# A release payload as the packaging lays it out: binary, data, the bundled
# installer and the release_info.json naming version and asset.
make_payload() { # dir asset [version]
    mkdir -p "$1/bin" "$1/assets/images" "$1/ui_xml" "$1/config"
    create_fake_elf "$1/bin/helix-screen" 02 3e
    chmod +x "$1/bin/helix-screen"
    : > "$1/assets/images/helix-icon-256.png"
    printf '#!/bin/sh\n' > "$1/install.sh"
    printf '{"project_name":"helixscreen","project_owner":"prestonbrown","version":"%s","asset_name":"%s"}\n' \
        "${3:-1.2.3-beta.4}" "$2" > "$1/release_info.json"
}

make_archive() { # asset name
    local pkg="$BATS_TEST_TMPDIR/pkg-$1/helixscreen"
    make_payload "$pkg" "$1"
    head -c 1100000 /dev/urandom > "$pkg/assets/pad.bin"
    tar -czf "$DL/$2" -C "$BATS_TEST_TMPDIR/pkg-$1" helixscreen
}

# Run main() as an x86 box whose installer sits in DIR.
run_main_from() { # dir args...
    local self="$1"
    shift
    installer_self_dir() { printf '%s\n' "$self"; }
    detect_platform() { echo x86; }
    detect_init_system() { INIT_SYSTEM=systemd; }
    run main "$@"
}

# ---------------------------------------------------------------------------
# An archive on the command line
# ---------------------------------------------------------------------------

@test "a positional .tar.gz or .zip that exists is --local" {
    : > "$DL/helixscreen-pi-v1.2.3.tar.gz"
    : > "$DL/helixscreen-pi.zip"
    parse_installer_args "$DL/helixscreen-pi-v1.2.3.tar.gz"
    [ "$local_tarball" = "$DL/helixscreen-pi-v1.2.3.tar.gz" ]
    parse_installer_args --desktop "$DL/helixscreen-pi.zip"
    [ "$local_tarball" = "$DL/helixscreen-pi.zip" ]
}

@test "a positional that is not an existing archive is still an unknown option" {
    run parse_installer_args "$DL/missing-v1.2.3.tar.gz"
    [ "$status" -eq 1 ]
    contains "Unknown option: $DL/missing-v1.2.3.tar.gz" "$output"
    : > "$DL/notes.txt"
    run parse_installer_args "$DL/notes.txt"
    [ "$status" -eq 1 ]
    contains "Unknown option" "$output"
    run parse_installer_args bogus
    [ "$status" -eq 1 ]
    contains "Unknown option: bogus" "$output"
}

@test "two different archives are refused" {
    : > "$DL/a-v1.tar.gz"
    : > "$DL/b-v2.tar.gz"
    run parse_installer_args --local "$DL/a-v1.tar.gz" "$DL/b-v2.tar.gz"
    [ "$status" -eq 1 ]
    contains "one archive" "$output"
}

@test "a positional archive installs with no channel or network lookup" {
    make_archive helixscreen-x86-desktop.zip helixscreen-x86-desktop-v1.2.3.tar.gz
    run_main_from "$BATS_TEST_TMPDIR/elsewhere" --desktop "$DL/helixscreen-x86-desktop-v1.2.3.tar.gz"
    [ "$status" -eq 0 ] || fail "main failed ($status): $output"
    [ -x "$DESK/bin/helix-screen" ]
    contains "Installing from local file" "$output"
    if reached_out; then fail "the --local run reached out: $(calls)"; fi
}

# ---------------------------------------------------------------------------
# The package the installer was unpacked with
# ---------------------------------------------------------------------------

@test "the installer installs the package it sits in, without the network" {
    make_payload "$DL/helixscreen" helixscreen-x86-desktop.zip
    echo pkg > "$DL/helixscreen/ui_xml/marker.txt"
    run_main_from "$DL/helixscreen" --desktop
    [ "$status" -eq 0 ] || fail "main failed ($status): $output"
    contains "Installing the package next to this installer: v1.2.3-beta.4 (x86-desktop)" "$output"
    [ "$(cat "$DESK/ui_xml/marker.txt")" = pkg ]
    desktop_root_is_ours "$DESK"
    # Copied, not moved: the user's extracted package is left as it was.
    [ "$(cat "$DL/helixscreen/ui_xml/marker.txt")" = pkg ]
    [ -x "$DL/helixscreen/bin/helix-screen" ]
    if reached_out; then fail "the package run reached out: $(calls)"; fi
}

@test "the package's own flavour decides desktop vs kiosk when nothing else does" {
    make_payload "$DL/helixscreen" helixscreen-x86-desktop.zip
    run_main_from "$DL/helixscreen" --dry-run
    [ "$status" -eq 0 ] || fail "dry run failed ($status): $output"
    contains "window on this desktop (the package)" "$output"
    contains "v1.2.3-beta.4" "$output"
    [ ! -e "$DESK" ]
    if reached_out; then fail "the dry run reached out: $(calls)"; fi
}

@test "the copy of the installer inside an installed tree resolves online" {
    make_payload "$DESK" helixscreen-x86-desktop.zip
    run_main_from "$DESK" --dry-run
    [ "$status" -eq 0 ] || fail "dry run failed ($status): $output"
    lacks "next to this installer" "$output"
    called "NETWORK get_latest_version" || fail "an installed tree's installer did not resolve online: $(calls)"
}

@test "a package at a known install root is an install, not a package" {
    make_payload "$BATS_TEST_TMPDIR/opt/helixscreen" helixscreen-x86.zip
    run_main_from "$BATS_TEST_TMPDIR/opt/helixscreen" --desktop --dry-run
    [ "$status" -eq 0 ] || fail "dry run failed ($status): $output"
    lacks "next to this installer" "$output"
    called "NETWORK get_latest_version" || fail "did not resolve online: $(calls)"
}

@test "a package built for another platform is refused, naming both" {
    make_payload "$DL/helixscreen" helixscreen-x86.zip
    run_main_from "$DL/helixscreen" --desktop
    [ "$status" -eq 1 ]
    contains "This package is for x86, but this computer needs x86-desktop" "$output"
    [ ! -e "$DESK" ]
    if reached_out; then fail "the refused run reached out: $(calls)"; fi
}

@test "a package for another board is refused on any platform" {
    make_payload "$DL/helixscreen" helixscreen-k2.zip
    installer_self_dir() { printf '%s\n' "$DL/helixscreen"; }
    platform=pi
    INSTALL_DIR="$BATS_TEST_TMPDIR/opt/helixscreen"
    detect_package_beside_installer
    [ "$package_dir" = "$DL/helixscreen" ]
    run settle_package_beside_installer
    [ "$status" -eq 1 ]
    contains "This package is for k2, but this computer needs pi" "$output"
}

@test "a directory without release_info.json is no package" {
    make_payload "$DL/helixscreen" helixscreen-x86-desktop.zip
    rm "$DL/helixscreen/release_info.json"
    installer_self_dir() { printf '%s\n' "$DL/helixscreen"; }
    detect_package_beside_installer
    [ -z "$package_dir" ]
}

@test "--version and --update beside a package still go online" {
    make_payload "$DL/helixscreen" helixscreen-x86-desktop.zip
    run_main_from "$DL/helixscreen" --desktop --dry-run --version v2.0.0
    [ "$status" -eq 0 ] || fail "dry run failed ($status): $output"
    lacks "next to this installer" "$output"
    called "NETWORK probe" || fail "--version did not probe online: $(calls)"

    : > "$BATS_TEST_TMPDIR/calls.log"
    run_main_from "$DL/helixscreen" --desktop --dry-run --update
    [ "$status" -eq 0 ] || fail "dry run failed ($status): $output"
    lacks "next to this installer" "$output"
    called "NETWORK get_latest_version" || fail "--update did not resolve online: $(calls)"
}

@test "an in-app update never takes the tree it runs from as a package" {
    make_payload "$DL/helixscreen" helixscreen-x86-desktop.zip
    installer_self_dir() { printf '%s\n' "$DL/helixscreen"; }
    HELIX_SELF_UPDATE=1
    detect_package_beside_installer
    [ -z "$package_dir" ]
}

# ---------------------------------------------------------------------------
# A version with no build for this platform
# ---------------------------------------------------------------------------

manifest() { # version assets...
    local v=$1 a
    shift
    printf '{\n  "version": "%s",\n  "assets": {\n' "$v"
    for a in "$@"; do
        printf '    "%s": {\n      "url": "https://example.invalid/%s.tar.gz"\n    },\n' "$a" "$a"
    done
    printf '    "zz": {}\n  }\n}\n'
}

stub_channels() {
    _fetch_channel_manifest() {
        case "$1" in
            stable) _R2_MANIFEST=$(manifest 1.0.5 pi x86) ;;
            beta) _R2_MANIFEST=$(manifest 1.1.0-beta.9 pi x86 x86-desktop) ;;
            *) return 1 ;;
        esac
    }
    _url_probe() { echo missing; }
}

@test "a missing build names the release that has this platform" {
    stub_channels
    platform=x86-desktop
    run probe_release v1.0.5 x86-desktop
    [ "$status" -eq 1 ]
    contains "No HelixScreen v1.0.5 release for x86-desktop." "$output"
    contains "--version v1.1.0-beta.9" "$output"
    contains "sh -s -- --desktop --version v1.1.0-beta.9" "$output"
}

@test "a missing build off the desktop flavour gives the command without --desktop" {
    stub_channels
    _fetch_channel_manifest() {
        case "$1" in
            stable) _R2_MANIFEST=$(manifest 1.0.5 pi) ;;
            beta) _R2_MANIFEST=$(manifest 1.1.0-beta.9 pi k2) ;;
            *) return 1 ;;
        esac
    }
    platform=k2
    run probe_release v1.0.5 k2
    [ "$status" -eq 1 ]
    contains "--version v1.1.0-beta.9" "$output"
    contains "sh -s -- --version v1.1.0-beta.9" "$output"
    lacks "--desktop" "$output"
}

@test "a missing build with no release found gives the generic --version hint" {
    _url_probe() { echo missing; }
    platform=x86-desktop
    run probe_release v1.0.5 x86-desktop
    [ "$status" -eq 1 ]
    contains "No HelixScreen v1.0.5 release for x86-desktop." "$output"
    contains "--version" "$output"
    lacks "v1.1.0-beta.9" "$output"
}

@test "a missing build never offers the version that just failed" {
    # The manifest lists the platform, yet every URL for it answered 404.
    _fetch_channel_manifest() { _R2_MANIFEST=$(manifest 1.0.5 pi x86-desktop); }
    _url_probe() { echo missing; }
    platform=x86-desktop
    run probe_release v1.0.5 x86-desktop
    [ "$status" -eq 1 ]
    lacks "--version v1.0.5" "$output"
    contains "--version vX.Y.Z" "$output"
}

@test "a package that is this run's INSTALL_DIR is the install, not a package" {
    make_payload "$HOME/helixscreen" helixscreen-pi.zip
    installer_self_dir() { printf '%s\n' "$HOME/helixscreen"; }
    platform=pi
    detect_package_beside_installer
    [ "$package_dir" = "$HOME/helixscreen" ]
    INSTALL_DIR="$HOME/helixscreen"
    settle_package_beside_installer
    [ -z "$package_dir" ]
}

# ---------------------------------------------------------------------------
# The channel a handed-over package puts the install on
# ---------------------------------------------------------------------------

@test "a prerelease package beside the installer puts a fresh install on beta" {
    make_payload "$DL/helixscreen" helixscreen-x86-desktop.zip 1.2.3-beta.4
    run_main_from "$DL/helixscreen" --dry-run
    [ "$status" -eq 0 ] || fail "dry run failed ($status): $output"
    contains "Update channel: beta (v1.2.3-beta.4 is a prerelease)" "$output"
    contains "v1.2.3-beta.4 (beta)" "$output"
}

@test "a prerelease archive named on the command line puts a fresh install on beta" {
    make_archive helixscreen-x86-desktop.zip helixscreen-x86-desktop-v1.2.3-beta.4.tar.gz
    run_main_from "$BATS_TEST_TMPDIR/elsewhere" --desktop --dry-run "$DL/helixscreen-x86-desktop-v1.2.3-beta.4.tar.gz"
    [ "$status" -eq 0 ] || fail "dry run failed ($status): $output"
    contains "Update channel: beta (v1.2.3-beta.4 is a prerelease)" "$output"
    contains "v1.2.3-beta.4 (beta)" "$output"
}

@test "a stable package leaves the channel alone" {
    make_payload "$DL/helixscreen" helixscreen-x86-desktop.zip 1.2.3
    run_main_from "$DL/helixscreen" --dry-run
    [ "$status" -eq 0 ] || fail "dry run failed ($status): $output"
    lacks "Update channel: beta" "$output"
    contains "v1.2.3 (stable)" "$output"
}

@test "the channel an existing install chose outranks a prerelease package" {
    make_payload "$DESK" helixscreen-x86-desktop.zip 1.2.2
    printf '{"config_version":3,"update":{"channel":0}}\n' > "$DESK/config/settings.json"
    make_payload "$DL/helixscreen" helixscreen-x86-desktop.zip 1.2.3-beta.4
    run_main_from "$DL/helixscreen" --dry-run
    [ "$status" -eq 0 ] || fail "dry run failed ($status): $output"
    lacks "Update channel: beta" "$output"
    contains "v1.2.3-beta.4 (stable)" "$output"
}

@test "an unversioned zip leaves the channel alone" {
    local pkg="$BATS_TEST_TMPDIR/zpkg"
    make_payload "$pkg" helixscreen-x86-desktop.zip 1.2.3-beta.4
    head -c 1100000 /dev/urandom > "$pkg/assets/pad.bin"
    (cd "$pkg" && python3 -c 'import os,sys,zipfile
z=zipfile.ZipFile(sys.argv[1],"w",zipfile.ZIP_DEFLATED)
for r,_,fs in os.walk("."):
    for f in fs: z.write(os.path.join(r,f))' "$DL/helixscreen-x86-desktop.zip")
    run_main_from "$BATS_TEST_TMPDIR/elsewhere" --desktop --dry-run "$DL/helixscreen-x86-desktop.zip"
    [ "$status" -eq 0 ] || fail "dry run failed ($status): $output"
    lacks "Update channel: beta" "$output"
    contains "local (stable)" "$output"
}

# ---------------------------------------------------------------------------
# What is and is not a package
# ---------------------------------------------------------------------------

@test "a flat zip unpacked among other files is not a package" {
    # The release zip has no top-level directory: unzipped in place, its
    # payload lands among whatever else the directory holds.
    make_payload "$DL" helixscreen-x86-desktop.zip
    echo mine > "$DL/tax-return.pdf"
    installer_self_dir() { printf '%s\n' "$DL"; }
    detect_package_beside_installer
    [ -z "$package_dir" ]

    run_main_from "$DL" --desktop --dry-run
    [ "$status" -eq 0 ] || fail "dry run failed ($status): $output"
    lacks "next to this installer" "$output"
    called "NETWORK get_latest_version" || fail "did not resolve online: $(calls)"
}

@test "the tar layout's helixscreen directory is a package" {
    make_payload "$DL/helixscreen" helixscreen-x86-desktop.zip
    installer_self_dir() { printf '%s\n' "$DL/helixscreen"; }
    detect_package_beside_installer
    [ "$package_dir" = "$DL/helixscreen" ]
}

@test "the copy keeps the package's symlinks and executable bits" {
    make_payload "$DL/helixscreen" helixscreen-x86-desktop.zip
    ln -s helix-screen "$DL/helixscreen/bin/helix-screen-link"
    run_main_from "$DL/helixscreen" --desktop
    [ "$status" -eq 0 ] || fail "main failed ($status): $output"
    [ -L "$DESK/bin/helix-screen-link" ]
    [ -x "$DESK/bin/helix-screen" ]
}

@test "a package whose size cannot be measured is refused, not sized as zero" {
    make_payload "$DL/helixscreen" helixscreen-x86-desktop.zip
    TMP_DIR="$BATS_TEST_TMPDIR/stage"
    INSTALL_DIR="$DESK"
    use_package_tree "$DL/helixscreen"
    _tree_size_mb() { :; }
    run extract_release x86-desktop
    [ "$status" -eq 1 ]
    contains "Could not measure the package" "$output"
    [ ! -e "$DESK" ]
}

# ---------------------------------------------------------------------------
# A channel named in the environment
# ---------------------------------------------------------------------------

env_channel() { R2_CHANNEL=$1; _R2_CHANNEL_FROM_ENV=yes; }

@test "R2_CHANNEL=beta on a fresh desktop install is recorded for the app" {
    export HELIX_STATE_VAR_LIB="$BATS_TEST_TMPDIR/var-lib" HELIX_STATE_ROOT_HOME="$BATS_TEST_TMPDIR/root-home"
    make_archive helixscreen-x86-desktop.zip helixscreen-x86-desktop-v1.2.3.tar.gz
    env_channel beta
    run_main_from "$BATS_TEST_TMPDIR/elsewhere" --desktop "$DL/helixscreen-x86-desktop-v1.2.3.tar.gz"
    [ "$status" -eq 0 ] || fail "main failed ($status): $output"
    [ "$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["update"]["channel"])' "$DESK/config/settings.json")" = 1 ] \
        || fail "no beta channel recorded: $(cat "$DESK/config/settings.json" 2>&1)"
    [ ! -e "$HELIX_STATE_VAR_LIB" ]
    [ ! -e "$HELIX_STATE_ROOT_HOME" ]
}

@test "R2_CHANNEL=beta never replaces the channel an existing install chose" {
    export HELIX_STATE_VAR_LIB="$BATS_TEST_TMPDIR/var-lib" HELIX_STATE_ROOT_HOME="$BATS_TEST_TMPDIR/root-home"
    make_payload "$DESK" helixscreen-x86-desktop.zip 1.2.2
    printf '{"config_version":3,"update":{"channel":0}}\n' > "$DESK/config/settings.json"
    make_archive helixscreen-x86-desktop.zip helixscreen-x86-desktop-v1.2.3.tar.gz
    env_channel beta
    run_main_from "$BATS_TEST_TMPDIR/elsewhere" --desktop "$DL/helixscreen-x86-desktop-v1.2.3.tar.gz"
    [ "$status" -eq 0 ] || fail "main failed ($status): $output"
    [ "$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["update"]["channel"])' "$DESK/config/settings.json")" = 0 ]
}

@test "R2_CHANNEL=beta reaches the desktop's own settings backup" {
    mkdir -p "$HOME/.helixscreen"
    printf '{"config_version":3}\n' > "$HOME/.helixscreen/settings.json.backup"
    platform=x86-desktop
    INSTALL_DIR="$DESK"
    mkdir -p "$DESK/config"
    env_channel beta
    match_channel_to_version v1.2.3
    seed_update_channel
    [ "$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["update"]["channel"])' "$HOME/.helixscreen/settings.json.backup")" = 1 ]
}

@test "R2_CHANNEL=stable records nothing" {
    env_channel stable
    match_channel_to_version v1.2.3
    [ -z "${_R2_CHANNEL_TO_SEED:-}" ]
}

# ---------------------------------------------------------------------------
# A desktop install of a beta records the beta channel, whichever way it came
# ---------------------------------------------------------------------------

desk_channel() { python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["update"]["channel"])' "$DESK/config/settings.json"; }

@test "desktop e2e: the beta package beside the installer records the beta channel" {
    export HELIX_STATE_VAR_LIB="$BATS_TEST_TMPDIR/var-lib" HELIX_STATE_ROOT_HOME="$BATS_TEST_TMPDIR/root-home"
    make_payload "$DL/helixscreen" helixscreen-x86-desktop.zip 1.2.3-beta.4
    run_main_from "$DL/helixscreen"
    [ "$status" -eq 0 ] || fail "main failed ($status): $output"
    [ "$(desk_channel)" = 1 ] || fail "no beta channel: $(cat "$DESK/config/settings.json" 2>&1)"
}

@test "desktop e2e: a beta archive named on the command line records the beta channel" {
    export HELIX_STATE_VAR_LIB="$BATS_TEST_TMPDIR/var-lib" HELIX_STATE_ROOT_HOME="$BATS_TEST_TMPDIR/root-home"
    make_archive helixscreen-x86-desktop.zip helixscreen-x86-desktop-v1.2.3-beta.4.tar.gz
    run_main_from "$BATS_TEST_TMPDIR/elsewhere" --desktop "$DL/helixscreen-x86-desktop-v1.2.3-beta.4.tar.gz"
    [ "$status" -eq 0 ] || fail "main failed ($status): $output"
    [ "$(desk_channel)" = 1 ] || fail "no beta channel: $(cat "$DESK/config/settings.json" 2>&1)"
}

@test "desktop e2e: --version of a beta records the beta channel" {
    export HELIX_STATE_VAR_LIB="$BATS_TEST_TMPDIR/var-lib" HELIX_STATE_ROOT_HOME="$BATS_TEST_TMPDIR/root-home"
    make_archive helixscreen-x86-desktop.zip helixscreen-x86-desktop-v1.2.3-beta.4.tar.gz
    # The CDN: the release exists, and downloading it stages the archive.
    _url_probe() { echo found; }
    download_release() { use_local_tarball "$DL/helixscreen-x86-desktop-v1.2.3-beta.4.tar.gz"; }
    run_main_from "$BATS_TEST_TMPDIR/elsewhere" --desktop --version v1.2.3-beta.4
    [ "$status" -eq 0 ] || fail "main failed ($status): $output"
    [ "$(desk_channel)" = 1 ] || fail "no beta channel: $(cat "$DESK/config/settings.json" 2>&1)"
}

@test "desktop e2e: a beta package keeps the channel an existing desktop install saved" {
    export HELIX_STATE_VAR_LIB="$BATS_TEST_TMPDIR/var-lib" HELIX_STATE_ROOT_HOME="$BATS_TEST_TMPDIR/root-home"
    make_payload "$DESK" helixscreen-x86-desktop.zip 1.2.2
    printf '{"config_version":3,"update":{"channel":0}}\n' > "$DESK/config/settings.json"
    make_payload "$DL/helixscreen" helixscreen-x86-desktop.zip 1.2.3-beta.4
    run_main_from "$DL/helixscreen"
    [ "$status" -eq 0 ] || fail "main failed ($status): $output"
    [ "$(desk_channel)" = 0 ]
}
