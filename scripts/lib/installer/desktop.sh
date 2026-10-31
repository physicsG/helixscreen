#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Module: desktop
# The x86 desktop flavour: HelixScreen in an SDL window beside the user's
# desktop, installed per user under the XDG data dir, with no service.
# A desktop run is platform "x86-desktop" from resolve_x86_flavour on; steps
# that touch the system test that value directly. A function predicate would
# be replaced by the installer-flow tests' stub-everything recorders.
#
# Reads: platform, X86_FLAVOUR_FLAG, update_mode, uninstall_mode, DRY_RUN,
#        ASSUME_YES, HOME, XDG_DATA_HOME, XDG_STATE_HOME, HELIX_SYSTEMD_UNIT_DIR
# Writes: platform, X86_FLAVOUR_REASON

# Source guard
[ -n "${_HELIX_DESKTOP_SOURCED:-}" ] && return 0
_HELIX_DESKTOP_SOURCED=1

# A home-relative root reaches .desktop files and the uninstaller unquoted, so
# a path that needs quoting is refused instead of escaped.
_desktop_path_ok() {
    case "$1" in
        /*) ;;
        *) return 1 ;;
    esac
    case "$1" in
        *..* | *[[:space:]]* | *[\"\'\`\$\\*?[]*) return 1 ;;
    esac
    return 0
}

# ${XDG_DATA_HOME:-$HOME/.local/share}, honouring the spec's rule that a
# relative XDG_DATA_HOME is ignored. Fails when neither yields a usable path.
_desktop_data_home() {
    case "${XDG_DATA_HOME:-}" in
        /*) printf '%s\n' "${XDG_DATA_HOME%/}"; return 0 ;;
    esac
    [ -n "${HOME:-}" ] && [ "$HOME" != "/" ] || return 1
    printf '%s/.local/share\n' "${HOME%/}"
}

# Where a desktop install lives. Not ~/helixscreen: that name collides with a
# git clone of this project and with a kiosk install at $KLIPPER_HOME.
desktop_install_root() {
    _dir_dh=$(_desktop_data_home) || return 1
    _desktop_path_ok "$_dir_dh/helixscreen" || return 1
    printf '%s/helixscreen\n' "$_dir_dh"
}

# The app-menu entry the install writes and the uninstall removes.
desktop_entry_path() {
    _dep_dh=$(_desktop_data_home) || return 1
    printf '%s/applications/helixscreen.desktop\n' "$_dep_dh"
}

# Where the installer keeps its log for a desktop install: outside the payload,
# which every update replaces.
desktop_state_dir() {
    case "${XDG_STATE_HOME:-}" in
        /*) printf '%s/helixscreen\n' "${XDG_STATE_HOME%/}"; return 0 ;;
    esac
    [ -n "${HOME:-}" ] && [ "$HOME" != "/" ] || return 1
    printf '%s/.local/state/helixscreen\n' "${HOME%/}"
}

# True when DIR holds the desktop build: its binary, and the release_info.json
# that names the desktop asset. A kiosk payload names helixscreen-x86.zip.
desktop_payload_is_ours() {
    _dpo="${1%/}"
    [ -e "$_dpo/bin/helix-screen" ] && [ -f "$_dpo/release_info.json" ] || return 1
    grep -Eq '"project_name"[[:space:]]*:[[:space:]]*"helixscreen"' "$_dpo/release_info.json" || return 1
    grep -Eq "\"asset_name\"[[:space:]]*:[[:space:]]*\"$(helix_self_update_asset x86-desktop)\"" \
        "$_dpo/release_info.json"
}

# The gate in front of every rm -rf of a desktop root: a real directory (not
# a symlink, not reached through one), named helixscreen, not $HOME, holding
# the desktop payload. A directory that merely shares the name is the user's.
desktop_root_is_ours() {
    _dro="${1%/}"
    _desktop_path_ok "$_dro" || return 1
    [ "${_dro##*/}" = "helixscreen" ] || return 1
    [ -d "$_dro" ] && [ ! -L "$_dro" ] || return 1
    [ "$(host_canonical_path "$_dro")" = "$(host_canonical_path "$(dirname "$_dro")")/helixscreen" ] || return 1
    if [ -n "${HOME:-}" ]; then
        [ "$(host_canonical_path "$_dro")" != "$(host_canonical_path "$HOME")" ] || return 1
    fi
    desktop_payload_is_ours "$_dro"
}

# True when this box carries a kiosk install: the service it installs, or a
# payload at a root the kiosk flavour uses. Every x86 install made before the
# desktop flavour existed is a kiosk install. A desktop tarball extracted by
# hand (into ~/helixscreen, say) is not one: it would turn a re-run of the
# one-liner into a screen takeover.
_x86_kiosk_install_exists() {
    _xk_unit_dir="${HELIX_SYSTEMD_UNIT_DIR:-/etc/systemd/system}"
    [ -f "$_xk_unit_dir/${SERVICE_NAME:-helixscreen}.service" ] && return 0
    [ -f "${HELIX_INIT_D_DIR:-/etc/init.d}/S90helixscreen" ] && return 0
    _xk_desk=$(desktop_install_root 2>/dev/null) || _xk_desk=""
    for _xk_dir in ${HOME:+$HOME/helixscreen} ${KLIPPER_HOME:+$KLIPPER_HOME/helixscreen} \
        ${HELIX_HOME_ROOT:-/home}/*/helixscreen $_HELIX_KNOWN_INSTALL_DIRS; do
        [ "$_xk_dir" != "$_xk_desk" ] || continue
        [ -x "$_xk_dir/bin/helix-screen" ] || continue
        desktop_payload_is_ours "$_xk_dir" || return 0
    done
    return 1
}

# True when a desktop session is on this box: this installer runs inside one,
# or a display manager is running or enabled.
x86_desktop_present() {
    _in_graphical_session && return 0
    command -v systemctl >/dev/null 2>&1 || return 1
    for _xdp_dm in $DISPLAY_MANAGERS; do
        _unit_is_competing "$_xdp_dm" && return 0
    done
    return 1
}

# Settle desktop vs kiosk for an x86 box, once, before anything reads the
# platform: on "desktop", platform becomes x86-desktop, and every later step
# dispatches on that. Precedence: --desktop/--kiosk, then an in-app update
# (always the kiosk), then the install already on disk (the flavour is
# decided at the first install and never re-asked), then the package beside
# the installer (detect_package_beside_installer), then a fresh install asks
# when a desktop session is present and a terminal can answer, else infers it.
# X86_FLAVOUR_REASON says which, for the plan.
resolve_x86_flavour() {
    X86_FLAVOUR_REASON=""
    if [ "$platform" != "x86" ]; then
        if [ -n "${X86_FLAVOUR_FLAG:-}" ]; then
            log_error "--${X86_FLAVOUR_FLAG} applies to x86_64 Linux only; this is a ${platform} system"
            exit 1
        fi
        return 0
    fi

    _rxf_desk_root=$(desktop_install_root 2>/dev/null) || _rxf_desk_root=""
    _rxf_desk=false
    _rxf_kiosk=false
    [ -n "$_rxf_desk_root" ] && desktop_root_is_ours "$_rxf_desk_root" && _rxf_desk=true
    _x86_kiosk_install_exists && _rxf_kiosk=true

    if [ -n "${X86_FLAVOUR_FLAG:-}" ]; then
        _rxf_choice="$X86_FLAVOUR_FLAG"
        X86_FLAVOUR_REASON="--$X86_FLAVOUR_FLAG"
        if [ "$_rxf_choice" = kiosk ] && [ "$_rxf_desk" = true ] && [ "${uninstall_mode:-false}" != true ]; then
            log_warn "A desktop install also exists at $_rxf_desk_root; this run leaves it alone."
            log_warn "Remove it with: --desktop --uninstall"
        fi
    elif _is_self_update; then
        # Only the kiosk app updates itself; the desktop app sends the user to
        # the release page instead.
        _rxf_choice=kiosk
        X86_FLAVOUR_REASON="in-app update"
    elif [ "$_rxf_desk" = true ] && [ "$_rxf_kiosk" = true ]; then
        log_error "This computer has both a desktop install ($_rxf_desk_root) and a kiosk install."
        log_error "Name the one this run is for: --desktop or --kiosk"
        exit 1
    elif [ "$_rxf_desk" = true ]; then
        _rxf_choice=desktop
        X86_FLAVOUR_REASON="existing install"
    elif [ "$_rxf_kiosk" = true ]; then
        _rxf_choice=kiosk
        X86_FLAVOUR_REASON="existing install"
    elif [ "${package_platform:-}" = x86-desktop ] && [ -n "${package_dir:-}" ]; then
        _rxf_choice=desktop
        X86_FLAVOUR_REASON="the package"
    elif [ "${package_platform:-}" = x86 ] && [ -n "${package_dir:-}" ]; then
        _rxf_choice=kiosk
        X86_FLAVOUR_REASON="the package"
    elif [ "${uninstall_mode:-false}" = true ]; then
        # Nothing of the desktop flavour to remove: the kiosk uninstall sweeps
        # every root a kiosk install could have used.
        _rxf_choice=kiosk
        X86_FLAVOUR_REASON="no desktop install found"
    elif [ "$(id -u)" = "0" ]; then
        # A desktop install belongs to a user and refuses root, so a root run
        # (sudo, a root shell, provisioning) is the kiosk it always was.
        _rxf_choice=kiosk
        X86_FLAVOUR_REASON="running as root"
    elif ! x86_desktop_present; then
        _rxf_choice=kiosk
        X86_FLAVOUR_REASON="no desktop session found"
    elif [ "${update_mode:-false}" = true ] || [ "${DRY_RUN:-false}" = true ] \
        || [ "${ASSUME_YES:-false}" = true ] || ! tty_can_ask; then
        _rxf_choice=desktop
        X86_FLAVOUR_REASON="desktop session found"
        [ "${DRY_RUN:-false}" = true ] && tty_can_ask \
            && X86_FLAVOUR_REASON="desktop session found; a real run asks"
    else
        printf '%s\n' "This computer has a desktop. HelixScreen can:" >&2
        printf '%s\n' "  - run in a window on this desktop (default), or" >&2
        printf '%s\n' "  - take over the screen as a kiosk, replacing the desktop at boot." >&2
        # End of input is no answer, not a no: "no" here takes over the screen.
        _rxf_rc=0
        tty_confirm "Run HelixScreen in a window on this desktop?" y abort || _rxf_rc=$?
        case "$_rxf_rc" in
            0) _rxf_choice=desktop ;;
            1) _rxf_choice=kiosk ;;
            *)
                log_error "No answer given. Nothing changed."
                exit 1
                ;;
        esac
        X86_FLAVOUR_REASON="you chose"
    fi

    if [ "$_rxf_choice" = desktop ]; then
        platform=x86-desktop
        log_info "Install flavour: desktop window ($X86_FLAVOUR_REASON)"
    else
        log_info "Install flavour: kiosk ($X86_FLAVOUR_REASON)"
    fi
}

# The plan's Mode line, empty off x86.
x86_flavour_plan_text() {
    [ -n "${X86_FLAVOUR_REASON:-}" ] || return 0
    if [ "${platform:-}" = "x86-desktop" ]; then
        printf 'window on this desktop (%s)' "$X86_FLAVOUR_REASON"
    else
        printf 'kiosk, takes over the screen (%s)' "$X86_FLAVOUR_REASON"
    fi
}

# Refuse a desktop run as root: its install belongs to the user whose desktop
# it opens on, and a root-owned tree in that user's home is one they cannot
# update or remove. Nothing a desktop install does needs privileges.
desktop_refuse_root() {
    [ "$(id -u)" != "0" ] && return 0
    log_error "The desktop install runs as your own user and needs no root."
    log_error "Run the installer again without sudo, as the user whose desktop it opens on."
    log_error "To take over the screen as a kiosk instead, add --kiosk."
    exit 1
}

# Refuse to install over a directory at the desktop root that is not a
# desktop install: the install swaps it aside and deletes it. Read-only, so it
# runs before the confirm point.
desktop_refuse_foreign_root() {
    [ -e "$INSTALL_DIR" ] || [ -L "$INSTALL_DIR" ] || return 0
    desktop_root_is_ours "$INSTALL_DIR" && return 0
    log_error "$INSTALL_DIR already exists and is not a HelixScreen desktop install."
    log_error "Move it away (or remove it) and run the installer again. Nothing changed."
    exit 1
}

# Write the freedesktop menu entry pointing at the install root. No autostart
# entry: the app opens when the user opens it.
write_desktop_entry() {
    _wde_path=$(desktop_entry_path) || { log_warn "No home directory; skipped the app menu entry"; return 1; }
    _wde_icon="$INSTALL_DIR/assets/images/helix-icon-256.png"
    mkdir -p "$(dirname "$_wde_path")" || return 1
    {
        printf '[Desktop Entry]\n'
        printf 'Type=Application\n'
        printf 'Name=HelixScreen\n'
        printf 'GenericName=3D Printer Control\n'
        printf 'Comment=Control a Klipper 3D printer\n'
        printf 'Exec=%s/bin/helix-screen\n' "$INSTALL_DIR"
        [ -f "$_wde_icon" ] && printf 'Icon=%s\n' "$_wde_icon"
        printf 'Terminal=false\n'
        printf 'Categories=Utility;\n'
    } > "$_wde_path.tmp.$$" && mv -f "$_wde_path.tmp.$$" "$_wde_path"
}

# Remove the menu entry, only when it launches the binary under ROOT: an entry
# of the same name the user or a package wrote is theirs.
remove_desktop_entry() { # root
    _rde_path=$(desktop_entry_path) || return 0
    [ -f "$_rde_path" ] || return 0
    if grep -qx "Exec=${1%/}/bin/helix-screen" "$_rde_path"; then
        rm -f "$_rde_path"
        log_note "Removed $(display_path "$_rde_path")"
    else
        log_warn "Left $(display_path "$_rde_path") alone: it does not launch ${1%/}/bin/helix-screen"
    fi
}

# Remove a desktop root after proving it is ours. Returns 1, removing nothing,
# when the directory exists and is not.
remove_desktop_root() { # root
    _rdr="${1%/}"
    [ -e "$_rdr" ] || [ -L "$_rdr" ] || return 0
    if ! desktop_root_is_ours "$_rdr"; then
        log_error "Refusing to remove $_rdr: it does not hold a HelixScreen desktop install"
        log_error "(expected bin/helix-screen and a release_info.json naming $(helix_self_update_asset x86-desktop))."
        return 1
    fi
    rm -rf "$_rdr"
}

# --uninstall of the desktop flavour: the install root, its update backups,
# the menu entry and the installer's log. Nothing outside the user's home.
uninstall_desktop() {
    _ud_root="${INSTALL_DIR%/}"
    if [ ! -e "$_ud_root" ]; then
        log_note "No desktop install at $(display_path "$_ud_root")"
    else
        remove_desktop_root "$_ud_root" || exit 1
        log_note "Removed $(display_path "$_ud_root")"
    fi
    for _ud_old in "$_ud_root.old" "$_ud_root.old."*; do
        [ -d "$_ud_old" ] || continue
        desktop_payload_is_ours "$_ud_old" && rm -rf "$_ud_old"
    done
    remove_desktop_entry "$_ud_root"
    if _ud_state=$(desktop_state_dir); then
        rm -f "$_ud_state/helixscreen-install.log" "$_ud_state/helixscreen-install.log.1"
        rmdir "$_ud_state" 2>/dev/null || true
    fi
    log_note "HelixScreen has been removed."
}
