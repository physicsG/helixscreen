#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Module: klipper_include
# Generic, data-driven per-printer Klipper-config include mechanism (#986).
#
# Some printers need a small extra Klipper config snippet to expose hardware
# HelixScreen wants to control (e.g. a camera light output_pin that the stock
# firmware doesn't declare). This module ships those snippets per printer-id
# and wires them into the user's printer.cfg via a marker-guarded [include]
# line, idempotently and reversibly.
#
# Bundled snippets live at: config/klipper_includes/<printer_id>.cfg
# Installed to:             <klipper config dir>/helixscreen/<printer_id>.cfg
#                           (~/printer_data/config on most firmwares;
#                            /etc/klipper/config on COSMOS -- see
#                            klipper_config_dir() in common.sh)
# printer.cfg gets:         [include helixscreen/<printer_id>.cfg]
#
# Both the copied cfg and the printer.cfg edit are recorded to
# ${INSTALL_DIR}/config/.klipper_includes so uninstall can undo them.
#
# Reads: KLIPPER_HOME / KLIPPER_CONFIG_DIR, INSTALL_DIR, SUDO
#        (and HELIX_KLIPPER_CFG_DIR for tests)
# Calls: file_sudo(), klipper_config_dir() from common.sh

# Source guard
[ -n "${_HELIX_KLIPPER_INCLUDE_SOURCED:-}" ] && return 0
_HELIX_KLIPPER_INCLUDE_SOURCED=1

# Resolve the directory holding bundled Klipper cfg snippets.
# Override with HELIX_KLIPPER_CFG_DIR (tests). Otherwise prefer the installed
# copy under ${INSTALL_DIR}/config/klipper_includes, then fall back to the
# in-repo source tree (dev installer running from a checkout).
_helix_klipper_cfg_dir() {
    if [ -n "${HELIX_KLIPPER_CFG_DIR:-}" ]; then
        echo "$HELIX_KLIPPER_CFG_DIR"
        return 0
    fi
    if [ -n "${INSTALL_DIR:-}" ] && [ -d "${INSTALL_DIR}/config/klipper_includes" ]; then
        echo "${INSTALL_DIR}/config/klipper_includes"
        return 0
    fi
    local _self_dir
    _self_dir="$(cd "$(dirname "$0")" 2>/dev/null && pwd)" || _self_dir=""
    if [ -n "$_self_dir" ] && [ -d "${_self_dir}/../../../config/klipper_includes" ]; then
        (cd "${_self_dir}/../../../config/klipper_includes" && pwd)
        return 0
    fi
    echo "${INSTALL_DIR:-/opt/helixscreen}/config/klipper_includes"
}

# Record an undo action to the include state file (deduplicated).
# Args: $1 = entry (e.g. "cfg:/path" or "include:/printer.cfg:helixscreen/x.cfg")
_record_klipper_include() {
    local entry="$1"
    local state_file="${INSTALL_DIR}/config/.klipper_includes"

    if [ -n "${INSTALL_DIR:-}" ] && [ ! -d "${INSTALL_DIR}/config" ]; then
        $(file_sudo "${INSTALL_DIR}") mkdir -p "${INSTALL_DIR}/config"
    fi

    if [ -f "$state_file" ] && grep -qF "$entry" "$state_file" 2>/dev/null; then
        return 0
    fi
    echo "$entry" | $(file_sudo "${INSTALL_DIR}/config") tee -a "$state_file" >/dev/null
}

# Install the bundled Klipper include for a printer-id.
#  1. copy config/klipper_includes/<id>.cfg → printer_data/config/helixscreen/<id>.cfg
#  2. ensure a marker-guarded [include helixscreen/<id>.cfg] line in printer.cfg
#  3. record both for uninstall
#
# Defensive: a missing bundled cfg, a missing printer_data/config, or a missing
# printer.cfg all warn-and-skip WITHOUT failing the install.
#
# Args: $1 = printer_id
install_klipper_include_for_printer() {
    local printer_id="$1"
    [ -n "$printer_id" ] || return 0

    local cfg_dir
    cfg_dir="$(_helix_klipper_cfg_dir)"
    local bundled="${cfg_dir}/${printer_id}.cfg"

    # Missing bundled cfg → nothing to include for this printer. Success.
    if [ ! -f "$bundled" ]; then
        return 0
    fi

    local pd_config
    pd_config="$(klipper_config_dir)"
    if [ -z "$pd_config" ]; then
        log_warn "Klipper config dir unknown; skipping Klipper include for ${printer_id}"
        return 0
    fi

    if [ ! -d "$pd_config" ]; then
        log_warn "No Klipper config dir at ${pd_config}; skipping Klipper include for ${printer_id}"
        return 0
    fi

    local printer_cfg="${pd_config}/printer.cfg"
    if [ ! -f "$printer_cfg" ]; then
        log_warn "printer.cfg not found at ${printer_cfg}; skipping Klipper include for ${printer_id}"
        return 0
    fi

    # --- 1. Copy the bundled cfg into printer_data/config/helixscreen/ ---
    local pd_helix="${pd_config}/helixscreen"
    if [ ! -d "$pd_helix" ]; then
        if ! $(file_sudo "$pd_config") mkdir -p "$pd_helix" 2>/dev/null; then
            log_warn "Could not create ${pd_helix}; skipping Klipper include for ${printer_id}"
            return 0
        fi
    fi

    local dest_cfg="${pd_helix}/${printer_id}.cfg"
    if $(file_sudo "$pd_helix") cp "$bundled" "$dest_cfg" 2>/dev/null; then
        log_info "Installed Klipper config: ${dest_cfg}"
        _record_klipper_include "cfg:${dest_cfg}"
    else
        log_warn "Could not copy ${bundled} to ${dest_cfg}; skipping include for ${printer_id}"
        return 0
    fi

    # --- 2. Ensure the marker-guarded include line in printer.cfg ---
    local include_line="[include helixscreen/${printer_id}.cfg]"
    if grep -qF "$include_line" "$printer_cfg" 2>/dev/null; then
        log_info "printer.cfg already includes helixscreen/${printer_id}.cfg"
    else
        # Append the include with a marker comment so uninstall (and humans) can
        # identify lines the installer added.
        {
            printf '\n'
            printf '%s\n' "# Added by HelixScreen installer (#986) -- helixscreen/${printer_id}.cfg"
            printf '%s\n' "$include_line"
        } | $(file_sudo "$printer_cfg") tee -a "$printer_cfg" >/dev/null

        if grep -qF "$include_line" "$printer_cfg" 2>/dev/null; then
            log_note "Added [include helixscreen/${printer_id}.cfg] to printer.cfg"
            _record_klipper_include "include:${printer_cfg}:helixscreen/${printer_id}.cfg"
            # A Klipper restart / firmware_restart is needed to pick up the new
            # include. We do not invent a restart path here; the installer's
            # platform recovery / service start handles process restarts, and a
            # firmware_restart is a user-visible printer action best left to the
            # operator after install.
            log_info "Klipper restart / firmware_restart needed to apply helixscreen/${printer_id}.cfg"
        else
            log_warn "Failed to add include line to printer.cfg for ${printer_id}"
        fi
    fi

    return 0
}
