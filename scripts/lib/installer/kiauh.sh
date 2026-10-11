#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Module: kiauh
# KIAUH extension auto-detection and installation
#
# Reads: INSTALL_DIR
# Writes: -

# Source guard
[ -n "${_HELIX_KIAUH_SOURCED:-}" ] && return 0
_HELIX_KIAUH_SOURCED=1

# Detect KIAUH extensions directory
# Returns the path to the extensions dir, or empty string if not found
detect_kiauh_dir() {
    # Check current user's home first (most common)
    if [ -d "$HOME/kiauh/kiauh/extensions" ]; then
        echo "$HOME/kiauh/kiauh/extensions"
        return 0
    fi

    # Scan /home/*/kiauh/kiauh/extensions/ for other users
    if [ -d "/home" ]; then
        for user_home in /home/*/kiauh/kiauh/extensions; do
            if [ -d "$user_home" ]; then
                echo "$user_home"
                return 0
            fi
        done
    fi

    # Not found
    echo ""
    return 0
}

# Where install_kiauh_extension would add the extension, for the plan. Sets
# KIAUH_DIR (empty when KIAUH is absent, the extension is already there, or
# --skip-kiauh-registration was given).
# shellcheck disable=SC2034  # KIAUH_DIR is read by main()'s plan
detect_kiauh() {
    KIAUH_DIR=""
    [ "${skip_kiauh_registration:-false}" = "true" ] && return 0
    KIAUH_DIR=$(detect_kiauh_dir)
    if [ -n "$KIAUH_DIR" ] && [ -d "$KIAUH_DIR/helixscreen" ]; then
        KIAUH_DIR=""
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

    # Copy extension files
    mkdir -p "$target_dir"
    cp "$src_dir/__init__.py" "$target_dir/"
    cp "$src_dir/helixscreen_extension.py" "$target_dir/"
    cp "$src_dir/metadata.json" "$target_dir/"

    if [ "$is_update" = true ]; then
        log_success "KIAUH extension updated at $target_dir (restart KIAUH to pick it up)"
    else
        log_success "KIAUH extension installed at $target_dir"
        # shellcheck disable=SC2034  # consumed by plan.sh (print_summary)
        KIAUH_EXT_ADDED=1
    fi
}
