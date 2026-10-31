#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Module: kiauh
# KIAUH extension auto-detection and installation
#
# Reads: INSTALL_DIR, SUDO
# Writes: KIAUH_DIR, KIAUH_SUDO, KIAUH_WRITE_SKIPPED, KIAUH_EXT_ADDED

# Source guard
[ -n "${_HELIX_KIAUH_SOURCED:-}" ] && return 0
_HELIX_KIAUH_SOURCED=1

# Home directory of user $1, or nothing for a name that is not a plain user
# name. Overridable in tests.
_kiauh_user_home() { # user
    case "$1" in
        "" | *[!A-Za-z0-9._-]*) return 0 ;;
    esac
    eval echo "~$1"
}

# The extensions dir under home $1, when KIAUH is there.
_kiauh_ext_under() { # home
    [ -n "$1" ] && [ -d "$1/kiauh/kiauh/extensions" ] && echo "$1/kiauh/kiauh/extensions"
    return 0
}

# Every KIAUH extensions dir under the homes root, one per line.
# KLIPPER_HOMES_ROOT is the test seam for /home.
_kiauh_scan() {
    local _d
    for _d in "${KLIPPER_HOMES_ROOT:-/home}"/*/kiauh/kiauh/extensions; do
        [ -d "$_d" ] && echo "$_d"
    done
    return 0
}

# Detect KIAUH extensions directory
# Returns the path to the extensions dir, or empty string if not found. KIAUH
# belongs to a person, so the homes that name one come first: the user who ran
# the installer (sudo's caller before $HOME, which sudo may have reset to
# root's), then the user Klipper runs as. A scan of every home is trusted only
# when it finds exactly one; with several, none is guessed at.
detect_kiauh_dir() {
    local _home _found
    for _home in "$(_kiauh_user_home "${SUDO_USER:-}")" "${HOME:-}" "${KLIPPER_HOME:-}"; do
        _found=$(_kiauh_ext_under "$_home")
        if [ -n "$_found" ]; then
            echo "$_found"
            return 0
        fi
    done
    _found=$(_kiauh_scan)
    if [ -n "$_found" ] && [ "$(printf '%s\n' "$_found" | grep -c .)" -eq 1 ]; then
        echo "$_found"
        return 0
    fi
    echo ""
    return 0
}

KIAUH_EXT_FILES="__init__.py helixscreen_extension.py metadata.json"

# True when this user can write the extension into $1 without escalating: the
# directory itself and every extension file already in it when it exists, its
# parent when it does not.
_kiauh_ext_writable() { # target_dir
    local _t="$1" _f
    if [ -d "$_t" ]; then
        [ -w "$_t" ] || return 1
        for _f in $KIAUH_EXT_FILES; do
            [ ! -e "$_t/$_f" ] || [ -w "$_t/$_f" ] || return 1
        done
        return 0
    fi
    [ -w "$(dirname "$_t")" ]
}

# The prefix that writes the extension into $1: empty when this user can, the
# installer's $SUDO when it cannot (KIAUH in another user's home). Returns 1
# when neither works: no sudo, or a self-update under NoNewPrivileges.
_kiauh_write_prefix() { # target_dir
    if _kiauh_ext_writable "$1"; then
        echo ""
        return 0
    fi
    [ -n "${SUDO:-}" ] || return 1
    if type _has_no_new_privs >/dev/null 2>&1 && _has_no_new_privs; then
        return 1
    fi
    echo "$SUDO"
}

# Where install_kiauh_extension would add the extension, for the plan. Sets
# KIAUH_DIR (empty when KIAUH is absent, the extension is already there, or
# --skip-kiauh-registration was given), KIAUH_SUDO (1 when writing it takes
# sudo) and KIAUH_WRITE_SKIPPED (the extensions dir when KIAUH is there but this
# run cannot write to it).
# shellcheck disable=SC2034  # read by main()'s plan
detect_kiauh() {
    KIAUH_DIR=""
    KIAUH_SUDO=""
    KIAUH_WRITE_SKIPPED=""
    [ "${skip_kiauh_registration:-false}" = "true" ] && return 0
    # KIAUH manages printer-host services, which a desktop install is not.
    [ "${platform:-}" = "x86-desktop" ] && return 0
    KIAUH_DIR=$(detect_kiauh_dir)
    if [ -z "$KIAUH_DIR" ]; then
        _dk_found=$(_kiauh_scan)
        if [ "$(printf '%s\n' "$_dk_found" | grep -c .)" -gt 1 ]; then
            log_warn "KIAUH is installed for more than one user; not adding the HelixScreen extension to any of them"
            log_warn "  Re-run the installer as the user whose KIAUH should list HelixScreen"
        fi
        return 0
    fi
    if [ -d "$KIAUH_DIR/helixscreen" ]; then
        KIAUH_DIR=""
    elif ! _dk_prefix=$(_kiauh_write_prefix "$KIAUH_DIR/helixscreen"); then
        KIAUH_WRITE_SKIPPED="$KIAUH_DIR"
        KIAUH_DIR=""
    elif [ -n "$_dk_prefix" ]; then
        KIAUH_SUDO=1
    fi
    return 0
}

# Install KIAUH extension for HelixScreen
# Args: $1 = skip flag ("true" to skip, anything else to install)
#
# Default behavior: if KIAUH is detected, install the extension automatically.
# Pass --skip-kiauh-registration to opt out. KIAUH itself sets this flag when
# it invokes install.sh from its own extension wrapper, since the extension
# files are already in place at that point.
install_kiauh_extension() {
    local skip="${1:-false}"
    local kiauh_ext_dir
    local src_dir="$INSTALL_DIR/scripts/kiauh/helixscreen"

    kiauh_ext_dir=$(detect_kiauh_dir)

    # No KIAUH installed — nothing to do
    if [ -z "$kiauh_ext_dir" ]; then
        return 0
    fi

    if [ "$skip" = "true" ]; then
        log_info "KIAUH detected at $kiauh_ext_dir — skipping extension (--skip-kiauh-registration)"
        return 0
    fi

    local target_dir="$kiauh_ext_dir/helixscreen"
    local is_update=false

    if [ -d "$target_dir" ]; then
        is_update=true
    fi

    # Check source files exist
    if [ ! -f "$src_dir/__init__.py" ] || [ ! -f "$src_dir/helixscreen_extension.py" ] || [ ! -f "$src_dir/metadata.json" ]; then
        log_warn "KIAUH extension source files not found at $src_dir"
        log_warn "  Release tarball is missing scripts/kiauh/. To register manually:"
        log_warn "    mkdir -p ~/kiauh/kiauh/extensions/helixscreen"
        log_warn "    cd ~/kiauh/kiauh/extensions/helixscreen"
        log_warn "    for f in __init__.py helixscreen_extension.py metadata.json; do"
        log_warn "      curl -sLO https://raw.githubusercontent.com/prestonbrown/helixscreen/main/scripts/kiauh/helixscreen/\$f"
        log_warn "    done"
        return 0
    fi

    # Already-installed-and-identical: don't touch a working tree
    if [ "$is_update" = true ] \
        && [ -f "$target_dir/__init__.py" ] \
        && [ -f "$target_dir/helixscreen_extension.py" ] \
        && [ -f "$target_dir/metadata.json" ] \
        && cmp -s "$src_dir/__init__.py" "$target_dir/__init__.py" \
        && cmp -s "$src_dir/helixscreen_extension.py" "$target_dir/helixscreen_extension.py" \
        && cmp -s "$src_dir/metadata.json" "$target_dir/metadata.json"; then
        log_info "KIAUH extension already up to date at $target_dir"
        return 0
    fi

    local prefix
    if ! prefix=$(_kiauh_write_prefix "$target_dir"); then
        log_warn "KIAUH extension not installed: $kiauh_ext_dir is not writable by $(id -un 2>/dev/null) and sudo is unavailable"
        log_warn "  Re-run the installer as the owner of $kiauh_ext_dir, or with sudo"
        return 0
    fi

    # Copy extension files
    local _f
    if ! $prefix mkdir -p "$target_dir" 2>/dev/null; then
        log_warn "KIAUH extension not installed: could not create $target_dir"
        return 0
    fi
    # Remove before copying: cp writes through a symlink at the destination,
    # and with sudo that would land wherever the link points.
    for _f in $KIAUH_EXT_FILES; do
        if ! { $prefix rm -f "$target_dir/$_f" && $prefix cp "$src_dir/$_f" "$target_dir/$_f"; } 2>/dev/null; then
            log_warn "KIAUH extension not installed: could not copy $_f into $target_dir"
            return 0
        fi
    done

    # Files written with sudo, or by root, belong to whoever owns KIAUH, or
    # its own updates cannot replace them.
    local owner
    owner=$(_file_owner_name "$kiauh_ext_dir")
    if [ -n "$owner" ] && [ "$owner" != "$(id -un 2>/dev/null)" ] \
        && { [ -n "$prefix" ] || [ "$(id -u)" = "0" ]; }; then
        if ! $prefix chown -R "${owner}:$(_resolve_primary_group "$owner")" "$target_dir" 2>/dev/null; then
            log_warn "KIAUH extension at $target_dir is not owned by $owner; KIAUH may not be able to update it"
        fi
    fi

    if [ "$is_update" = true ]; then
        log_success "KIAUH extension updated at $target_dir (restart KIAUH to pick it up)"
    else
        log_success "KIAUH extension installed at $target_dir"
        # shellcheck disable=SC2034  # consumed by plan.sh (print_summary)
        KIAUH_EXT_ADDED=1
    fi
}
