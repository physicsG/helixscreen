#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# HelixPrint Moonraker Plugin Installer
#
# This script installs the helix_print Moonraker plugin.
#
# Usage:
#   ./install.sh              # Interactive install (manual config steps)
#   ./install.sh --auto       # Full auto-install (updates config, restarts Moonraker)
#   ./install.sh --uninstall  # Remove the plugin
#
# Remote install (from GitHub):
#   curl -sSL https://raw.githubusercontent.com/prestonbrown/helixscreen/main/moonraker-plugin/remote-install.sh | sh

set -e

# Get script directory (POSIX-compatible)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLUGIN_FILE="$SCRIPT_DIR/helix_print.py"
AUTO_MODE=false

# Exit codes for --uninstall and --uninstall-auto:
#   0 - uninstalled; nothing left in the config to clean up
#   2 - the plugin was removed, but the phase-tracking strip skipped or
#       failed a config file, which needs a human to look at it
#   1 (or any other nonzero) - the uninstall itself failed; the plugin may
#       still be present
EXIT_UNINSTALL_OK=0
EXIT_UNINSTALL_FAILED=1
EXIT_UNINSTALL_NEEDS_ATTENTION=2

# Colors for output (works with printf)
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info() {
    printf "${GREEN}[INFO]${NC} %s\n" "$1"
}

warn() {
    printf "${YELLOW}[WARN]${NC} %s\n" "$1"
}

error() {
    printf "${RED}[ERROR]${NC} %s\n" "$1"
    exit 1
}

# Find Moonraker installation
find_moonraker() {
    moonraker_path=""
    user_path="$1"

    # Check common locations (iterate without arrays)
    for loc in \
        "$HOME/moonraker" \
        "$HOME/klipper_config/moonraker" \
        "/home/pi/moonraker" \
        "/home/klipper/moonraker" \
        "/root/printer_software/moonraker" \
        "$user_path"
    do
        if [ -n "$loc" ] && [ -d "$loc/moonraker/components" ]; then
            moonraker_path="$loc"
            break
        fi
    done

    # Also check if moonraker is installed as a package
    if [ -z "$moonraker_path" ]; then
        pip_loc=$(python3 -c "import moonraker; print(moonraker.__path__[0])" 2>/dev/null || true)
        if [ -n "$pip_loc" ] && [ -d "$pip_loc/components" ]; then
            moonraker_path="$(dirname "$pip_loc")"
        fi
    fi

    printf '%s' "$moonraker_path"
}

# Find config directory
find_config_dir() {
    for loc in \
        "$HOME/printer_data/config" \
        "$HOME/klipper_config" \
        "/home/pi/printer_data/config" \
        "/home/pi/klipper_config" \
        "/root/printer_data/config"
    do
        if [ -d "$loc" ] && [ -f "$loc/moonraker.conf" ]; then
            printf '%s' "$loc"
            return 0
        fi
    done
    printf ''
}

# Main installation
main() {
    info "HelixPrint Moonraker Plugin Installer"
    printf '\n'

    # Check plugin file exists
    if [ ! -f "$PLUGIN_FILE" ]; then
        error "Plugin file not found: $PLUGIN_FILE"
    fi

    # Find Moonraker
    moonraker_path=$(find_moonraker "$1")

    if [ -z "$moonraker_path" ]; then
        error "Could not find Moonraker installation.
Please provide the path: ./install.sh /path/to/moonraker"
    fi

    components_dir="$moonraker_path/moonraker/components"

    if [ ! -d "$components_dir" ]; then
        error "Components directory not found: $components_dir"
    fi

    info "Found Moonraker at: $moonraker_path"
    info "Components directory: $components_dir"
    printf '\n'

    # Create symlink
    target="$components_dir/helix_print.py"

    if [ -f "$target" ] && [ ! -L "$target" ]; then
        error "A file (not symlink) exists at $target
Please remove it manually before installing."
    fi

    # Use ln -sf for atomic replacement (removes existing symlink first)
    ln -sf "$PLUGIN_FILE" "$target"
    info "Created symlink: $target -> $PLUGIN_FILE"
    printf '\n'

    # Remind about configuration
    info "Installation complete!"
    printf '\n'
    printf '%s\n' "Next steps:"
    printf '%s\n' "  1. Add the following to your moonraker.conf:"
    printf '\n'
    printf '%s\n' "     [helix_print]"
    printf '%s\n' "     # enabled: True"
    printf '%s\n' "     # temp_dir: .helix_temp"
    printf '%s\n' "     # symlink_dir: .helix_print"
    printf '%s\n' "     # cleanup_delay: 86400"
    printf '\n'
    printf '%s\n' "  2. Restart Moonraker:"
    printf '%s\n' "     sudo systemctl restart moonraker"
    printf '\n'
    printf '%s\n' "  3. Verify the plugin is loaded:"
    printf '%s\n' "     curl http://localhost:7125/server/helix/status"
    printf '\n'
}

# Uninstall function (interactive)
uninstall() {
    info "Uninstalling HelixPrint plugin..."
    exit_status=$EXIT_UNINSTALL_OK

    moonraker_path=$(find_moonraker "$1")

    if [ -z "$moonraker_path" ]; then
        error "Could not find Moonraker installation."
    fi

    target="$moonraker_path/moonraker/components/helix_print.py"

    if [ -L "$target" ]; then
        rm "$target"
        info "Removed symlink: $target"
    elif [ -f "$target" ]; then
        warn "Found regular file (not symlink) at $target"
        warn "Please remove it manually if desired."
    else
        info "Plugin symlink not found (already uninstalled?)"
    fi

    config_dir=$(find_config_dir)
    if [ -n "$config_dir" ]; then
        # The plugin itself is already gone at this point, so any nonzero
        # here - a skipped file or a failed one - is "needs attention", not
        # a failed uninstall.
        strip_phase_tracking_instrumentation "$config_dir" || exit_status=$EXIT_UNINSTALL_NEEDS_ATTENTION
    fi

    printf '\n'
    printf '%s\n' "Don't forget to:"
    printf '%s\n' "  1. Remove [helix_print] section from moonraker.conf"
    printf '%s\n' "  2. Restart Moonraker: sudo systemctl restart moonraker"

    return "$exit_status"
}

# Wait for Moonraker to become available after restart
wait_for_moonraker() {
    [ "$NO_RESTART" = "true" ] && return 0
    max_attempts=30
    attempt=0
    moonraker_url="${MOONRAKER_URL:-http://localhost:7125}"

    info "Waiting for Moonraker to become available..."

    while [ "$attempt" -lt "$max_attempts" ]; do
        if curl -s --max-time 2 "$moonraker_url/server/info" > /dev/null 2>&1; then
            info "Moonraker is ready!"
            return 0
        fi
        attempt=$((attempt + 1))
        sleep 1
    done

    warn "Moonraker did not respond within ${max_attempts} seconds"
    return 1
}

# Restart Moonraker service
restart_moonraker() {
    # A caller under systemd's NoNewPrivileges (the HelixScreen service) cannot
    # sudo, so it passes --no-restart and asks Moonraker to restart itself.
    if [ "$NO_RESTART" = "true" ]; then
        info "Leaving the Moonraker restart to the caller"
        return 0
    fi
    info "Restarting Moonraker..."
    if command -v systemctl > /dev/null 2>&1 && systemctl list-units --type=service 2>/dev/null | grep -q moonraker; then
        sudo systemctl restart moonraker || warn "Failed to restart Moonraker via systemctl"
    elif command -v service > /dev/null 2>&1; then
        sudo service moonraker restart || warn "Failed to restart Moonraker via service"
    else
        warn "Neither systemctl nor service found - please restart Moonraker manually"
    fi
}

# Strip phase-tracking instrumentation from PRINT_START via the bundled
# strip_phase_tracking.py, which owns the marker format and the safety
# contract around editing a printer's config (matched blocks only, a
# verified backup, symlinks and modes preserved). python3 is what Moonraker
# itself runs on; its absence here means something else on this printer is
# already broken. Either way this never aborts the rest of uninstall - it
# reports the outcome and returns it, so the caller can fold it into its own
# exit status instead of claiming success it cannot back up.
#
# Returns strip_phase_tracking.py's own exit status unchanged (0 clean, 2
# needs attention, 1 failed) so a caller can wire it straight through; a
# missing python3 is reported as EXIT_UNINSTALL_NEEDS_ATTENTION since
# PRINT_START could not be checked at all.
strip_phase_tracking_instrumentation() {
    scan_dir="$1"

    if ! command -v python3 > /dev/null 2>&1; then
        warn "python3 not found - could not check PRINT_START for phase-tracking instrumentation"
        warn "If a previous HelixScreen instrumented PRINT_START, remove any block between"
        warn "'# <<< HELIX_TRACKING v2 >>>' and '# <<< /HELIX_TRACKING >>>' by hand, then restart Klipper"
        return "$EXIT_UNINSTALL_NEEDS_ATTENTION"
    fi

    strip_status=$EXIT_UNINSTALL_OK
    python3 "$SCRIPT_DIR/strip_phase_tracking.py" "$scan_dir" || strip_status=$?
    if [ "$strip_status" -ne 0 ]; then
        warn "Phase-tracking strip reported a failure - see the output above"
    fi
    return "$strip_status"
}

# Auto-uninstall function (non-interactive, for HelixScreen integration)
auto_uninstall() {
    info "HelixPrint Auto-Uninstall Mode"
    printf '\n'
    exit_status=$EXIT_UNINSTALL_OK

    # Find Moonraker (auto-detect only)
    moonraker_path=$(find_moonraker "")

    if [ -z "$moonraker_path" ]; then
        error "Could not auto-detect Moonraker installation."
    fi

    target="$moonraker_path/moonraker/components/helix_print.py"

    # Find config directory
    config_dir=$(find_config_dir)

    # Remove symlink
    if [ -L "$target" ]; then
        rm "$target"
        info "Removed symlink: $target"
    elif [ -f "$target" ]; then
        error "Found regular file (not symlink) at $target - manual removal required"
    else
        info "Plugin symlink not found (already uninstalled?)"
    fi

    # helix_macros.cfg and its printer.cfg include are left in place: the
    # macros are shared HelixScreen helpers (HELIX_START_PRINT,
    # HELIX_CLEAN_NOZZLE and friends) that keep working without this plugin.
    # Any HELIX_PHASE_*/HELIX_READY calls this plugin's own instrumentation
    # left in PRINT_START are removed, with a backup, by the call below. The
    # plugin itself is already gone at this point, so any nonzero here - a
    # skipped file or a failed one - is "needs attention", not a failed
    # uninstall.
    if [ -n "$config_dir" ]; then
        strip_phase_tracking_instrumentation "$config_dir" || exit_status=$EXIT_UNINSTALL_NEEDS_ATTENTION
    fi

    # Remove config section if possible. The plugin itself is already gone
    # at this point, so a failure backing up or editing moonraker.conf here
    # is "needs attention", not a failed uninstall, and must not trip set -e
    # and skip the rest of this function.
    if [ -n "$config_dir" ] && [ -f "$config_dir/moonraker.conf" ]; then
        moonraker_conf="$config_dir/moonraker.conf"

        if grep -q '^\[helix_print\]' "$moonraker_conf"; then
            backup_file="${moonraker_conf}.bak.$(date +%Y%m%d_%H%M%S)"
            if cp "$moonraker_conf" "$backup_file"; then
                info "Created backup: $backup_file"

                info "Removing [helix_print] section from moonraker.conf"
                # Use awk for cross-platform config section removal
                # This correctly handles helix_print as the last section in the file
                if ! (awk '
                    /^\[helix_print\]/ { skip = 1; next }
                    /^\[/ { skip = 0 }
                    !skip { print }
                ' "$moonraker_conf" > "$moonraker_conf.tmp" && mv "$moonraker_conf.tmp" "$moonraker_conf"); then
                    warn "Could not remove [helix_print] from moonraker.conf - see $backup_file"
                    exit_status=$EXIT_UNINSTALL_NEEDS_ATTENTION
                fi
            else
                warn "Could not back up moonraker.conf - leaving the [helix_print] section in place"
                exit_status=$EXIT_UNINSTALL_NEEDS_ATTENTION
            fi
        fi
    fi

    # Restart Moonraker
    restart_moonraker

    # Wait for Moonraker to come back up. A slow or unreachable restart here
    # must not turn a completed uninstall into a reported failure: the
    # plugin is already gone, and this timeout is about restart timing, not
    # the uninstall's own outcome.
    wait_for_moonraker || warn "Moonraker did not come back up in time, but the plugin removal itself completed"

    printf '\n'
    if [ "$exit_status" -eq "$EXIT_UNINSTALL_OK" ]; then
        info "Auto-uninstall complete!"
    else
        warn "Auto-uninstall complete, but PRINT_START needs a look - see the warnings above"
    fi

    return "$exit_status"
}

# Auto-install function (non-interactive, for HelixScreen integration)
auto_install() {
    info "HelixPrint Auto-Install Mode"
    printf '\n'

    AUTO_MODE=true

    # Check plugin file exists
    if [ ! -f "$PLUGIN_FILE" ]; then
        error "Plugin file not found: $PLUGIN_FILE"
    fi

    # Find Moonraker (auto-detect only)
    moonraker_path=$(find_moonraker "")

    if [ -z "$moonraker_path" ]; then
        error "Could not auto-detect Moonraker installation."
    fi

    components_dir="$moonraker_path/moonraker/components"

    # Find config directory
    config_dir=$(find_config_dir)

    info "Moonraker: $moonraker_path"
    info "Config: ${config_dir:-not found}"
    printf '\n'

    # Pre-flight permission checks
    if [ ! -w "$components_dir" ]; then
        error "Cannot write to components directory: $components_dir"
    fi

    if [ -n "$config_dir" ] && [ -f "$config_dir/moonraker.conf" ]; then
        if [ ! -w "$config_dir/moonraker.conf" ]; then
            error "Cannot write to moonraker.conf: $config_dir/moonraker.conf"
        fi
    fi

    # Create symlink
    target="$components_dir/helix_print.py"

    if [ -f "$target" ] && [ ! -L "$target" ]; then
        error "A file (not symlink) exists at $target"
    fi

    # Use ln -sf for atomic replacement (removes existing symlink first)
    ln -sf "$PLUGIN_FILE" "$target"
    info "Created symlink: $target"

    # Auto-configure moonraker.conf if possible
    if [ -n "$config_dir" ] && [ -f "$config_dir/moonraker.conf" ]; then
        moonraker_conf="$config_dir/moonraker.conf"

        # Check if [helix_print] section already exists
        if grep -q '^\[helix_print\]' "$moonraker_conf"; then
            info "Config section [helix_print] already exists"
        else
            # Create backup before modifying config
            backup_file="${moonraker_conf}.bak.$(date +%Y%m%d_%H%M%S)"
            cp "$moonraker_conf" "$backup_file"
            info "Created backup: $backup_file"

            info "Adding [helix_print] section to moonraker.conf"
            printf '\n' >> "$moonraker_conf"
            printf '%s\n' "[helix_print]" >> "$moonraker_conf"
            printf '%s\n' "# HelixScreen plugin - auto-configured" >> "$moonraker_conf"
        fi
    else
        warn "Could not find moonraker.conf - manual config required"
    fi

    # Restart Moonraker
    restart_moonraker

    # Wait for Moonraker to come back up
    wait_for_moonraker

    printf '\n'
    info "Auto-install complete!"
}

show_help() {
    printf '%s\n' "Usage: $0 [OPTIONS] [MOONRAKER_PATH]"
    printf '\n'
    printf '%s\n' "Options:"
    printf '%s\n' "  --auto, -a              Full auto-install (updates config, restarts Moonraker)"
    printf '%s\n' "  --no-restart            Before --auto/--uninstall-auto: skip the Moonraker restart;"
    printf '%s\n' "                          the caller restarts it (e.g. via Moonraker's server.restart)"
    printf '%s\n' "  --uninstall, -u         Remove the plugin symlink and strip PRINT_START"
    printf '%s\n' "                          instrumentation, if any (interactive)"
    printf '%s\n' "  --uninstall-auto        Full auto-uninstall: removes the symlink and config"
    printf '%s\n' "                          section, strips PRINT_START instrumentation, restarts"
    printf '%s\n' "                          Moonraker"
    printf '%s\n' "  --help, -h              Show this help message"
    printf '\n'
    printf '%s\n' "Exit codes for --uninstall and --uninstall-auto:"
    printf '%s\n' "  0   uninstalled; nothing left in the config to clean up"
    printf '%s\n' "  2   the plugin was removed, but a config file was skipped or failed during"
    printf '%s\n' "      the phase-tracking strip and needs a human to look at it"
    printf '%s\n' "  1   (or any other nonzero) the uninstall itself failed"
    printf '\n'
    printf '%s\n' "Arguments:"
    printf '%s\n' "  MOONRAKER_PATH     Path to Moonraker installation (auto-detected if not provided)"
    printf '\n'
    printf '%s\n' "Environment Variables:"
    printf '%s\n' "  MOONRAKER_URL      URL for Moonraker API health check after restart"
    printf '%s\n' "                     Default: http://localhost:7125"
    printf '%s\n' "                     Example: MOONRAKER_URL=http://192.168.1.100:7125 ./install.sh --auto"
    printf '\n'
    printf '%s\n' "Examples:"
    printf '%s\n' "  ./install.sh --auto      # Auto-install (updates moonraker.conf, restarts Moonraker)"
}

# Parse arguments
NO_RESTART=false
while [ $# -gt 0 ]; do
    case "$1" in
        --no-restart)
            NO_RESTART=true
            shift
            ;;
        --auto|-a)
            AUTO_MODE=true
            shift
            ;;
        --uninstall|-u)
            uninstall "$2"
            exit $?
            ;;
        --uninstall-auto)
            auto_uninstall
            exit $?
            ;;
        --help|-h)
            show_help
            exit 0
            ;;
        *)
            # Assume it's a moonraker path for interactive mode
            if [ "$AUTO_MODE" = "true" ]; then
                auto_install
            else
                main "$1"
            fi
            exit 0
            ;;
    esac
done

# No arguments left - run in appropriate mode
if [ "$AUTO_MODE" = "true" ]; then
    auto_install
else
    main ""
fi
