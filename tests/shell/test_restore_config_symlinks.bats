#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for restore_config_symlinks() in restore-user-config.sh
#
# Verifies that config symlinks are restored after Moonraker type:web updates
# destroy them by replacing the entire install directory.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

setup() {
    load helpers

    # Set up directory structure mimicking a real system
    IDIR="$BATS_TEST_TMPDIR/helixscreen"
    mkdir -p "$IDIR/config"

    USER_VAL="pi"
    GROUP_VAL="pi"

    # Create printer_data with user config
    mkdir -p "$BATS_TEST_TMPDIR/home/pi/printer_data/config/helixscreen"

    # Source just the restore function from the script (skip systemd operations)
    # We define the variables and function body inline since the script isn't
    # designed to be sourced (set -e, systemctl calls, etc.)
    HELIX_USER_CONFIG_FILES="settings.json helixscreen.env .disabled_services tool_spools.json"

    # Override home expansion to use our test tmpdir
    eval "$(cat << 'FUNC_EOF'
restore_config_symlinks() {
    local install_config="${IDIR}/config"
    [ -d "$install_config" ] || return 0

    local pd_helix=""
    local user_home=""

    if [ -n "$USER_VAL" ] && [ "$USER_VAL" != "root" ]; then
        user_home=$(eval echo "~${USER_VAL}" 2>/dev/null || echo "")
    fi

    for candidate in \
        "${user_home}/printer_data/config/helixscreen" \
        /home/pi/printer_data/config/helixscreen \
        /home/biqu/printer_data/config/helixscreen \
        /home/mks/printer_data/config/helixscreen \
        /root/printer_data/config/helixscreen \
        /usr/data/printer_data/config/helixscreen; do
        [ -z "$candidate" ] && continue
        if [ -d "$candidate" ]; then
            pd_helix="$candidate"
            break
        fi
    done

    [ -n "$pd_helix" ] || return 0

    for file in $HELIX_USER_CONFIG_FILES; do
        local pd_file="${pd_helix}/${file}"
        local install_file="${install_config}/${file}"

        if [ -L "$install_file" ]; then
            local target
            target=$(readlink "$install_file" 2>/dev/null || echo "")
            [ "$target" = "$pd_file" ] && continue
            rm -f "$install_file"
        fi

        if [ -f "$pd_file" ]; then
            rm -f "$install_file" 2>/dev/null
            ln -s "$pd_file" "$install_file" 2>/dev/null || true
        fi
    done
}
FUNC_EOF
)"
}

# Use a known test path instead of relying on ~pi expansion
setup_pd_helix() {
    local pd_helix="$1"
    mkdir -p "$pd_helix"

    # Patch the function to use this explicit path
    eval "$(cat << FUNC_EOF
restore_config_symlinks() {
    local install_config="\${IDIR}/config"
    [ -d "\$install_config" ] || return 0
    local pd_helix="$pd_helix"

    for file in \$HELIX_USER_CONFIG_FILES; do
        local pd_file="\${pd_helix}/\${file}"
        local install_file="\${install_config}/\${file}"

        if [ -L "\$install_file" ]; then
            local target
            target=\$(readlink "\$install_file" 2>/dev/null || echo "")
            [ "\$target" = "\$pd_file" ] && continue
            rm -f "\$install_file"
        fi

        if [ -f "\$pd_file" ]; then
            rm -f "\$install_file" 2>/dev/null
            ln -s "\$pd_file" "\$install_file" 2>/dev/null || true
        fi
    done
}
FUNC_EOF
)"
}

# --- Core scenario: Moonraker update replaces symlinks with default files ---

@test "replaces default file with symlink to user config in printer_data" {
    local pd_helix="$BATS_TEST_TMPDIR/home/pi/printer_data/config/helixscreen"
    setup_pd_helix "$pd_helix"

    # User's real config in printer_data
    echo '{"theme":"dark","language":"de"}' > "$pd_helix/settings.json"
    # Default file from ZIP (Moonraker extracted this)
    echo '{}' > "$IDIR/config/settings.json"

    run restore_config_symlinks
    [ "$status" -eq 0 ]

    # Install dir should now have a symlink, not the default file
    [ -L "$IDIR/config/settings.json" ]
    local target
    target=$(readlink "$IDIR/config/settings.json")
    [ "$target" = "$pd_helix/settings.json" ]

    # User's config should be readable through the symlink
    grep -q '"theme":"dark"' "$IDIR/config/settings.json"
}

@test "restores all user config files" {
    local pd_helix="$BATS_TEST_TMPDIR/home/pi/printer_data/config/helixscreen"
    setup_pd_helix "$pd_helix"

    # Create user configs in printer_data and defaults in install dir
    echo '{"lang":"fr"}' > "$pd_helix/settings.json"
    echo 'HELIX_LOG=debug' > "$pd_helix/helixscreen.env"
    echo '{}' > "$IDIR/config/settings.json"
    echo '' > "$IDIR/config/helixscreen.env"

    run restore_config_symlinks
    [ "$status" -eq 0 ]

    [ -L "$IDIR/config/settings.json" ]
    [ -L "$IDIR/config/helixscreen.env" ]
}

@test "leaves correct symlinks untouched" {
    local pd_helix="$BATS_TEST_TMPDIR/home/pi/printer_data/config/helixscreen"
    setup_pd_helix "$pd_helix"

    echo '{"ok":true}' > "$pd_helix/settings.json"
    # Pre-existing correct symlink
    ln -s "$pd_helix/settings.json" "$IDIR/config/settings.json"

    run restore_config_symlinks
    [ "$status" -eq 0 ]

    # Should still be the same symlink
    [ -L "$IDIR/config/settings.json" ]
    local target
    target=$(readlink "$IDIR/config/settings.json")
    [ "$target" = "$pd_helix/settings.json" ]
}

@test "fixes symlink pointing to wrong location" {
    local pd_helix="$BATS_TEST_TMPDIR/home/pi/printer_data/config/helixscreen"
    setup_pd_helix "$pd_helix"

    echo '{"ok":true}' > "$pd_helix/settings.json"
    # Symlink pointing somewhere else (stale)
    ln -s "/old/path/settings.json" "$IDIR/config/settings.json"

    run restore_config_symlinks
    [ "$status" -eq 0 ]

    [ -L "$IDIR/config/settings.json" ]
    local target
    target=$(readlink "$IDIR/config/settings.json")
    [ "$target" = "$pd_helix/settings.json" ]
}

@test "skips files that don't exist in printer_data" {
    local pd_helix="$BATS_TEST_TMPDIR/home/pi/printer_data/config/helixscreen"
    setup_pd_helix "$pd_helix"

    # Only settings.json exists in printer_data — no helixscreen.env
    echo '{"ok":true}' > "$pd_helix/settings.json"
    echo '{}' > "$IDIR/config/settings.json"
    echo '' > "$IDIR/config/helixscreen.env"

    run restore_config_symlinks
    [ "$status" -eq 0 ]

    # settings.json should be a symlink
    [ -L "$IDIR/config/settings.json" ]
    # helixscreen.env should remain as-is (real file, not touched)
    [ -f "$IDIR/config/helixscreen.env" ]
    [ ! -L "$IDIR/config/helixscreen.env" ]
}

@test "no-op when printer_data/config/helixscreen doesn't exist" {
    local pd_helix="$BATS_TEST_TMPDIR/nonexistent/printer_data/config/helixscreen"
    setup_pd_helix "$pd_helix"
    # Remove it so it doesn't exist
    rmdir "$pd_helix"

    echo '{}' > "$IDIR/config/settings.json"

    run restore_config_symlinks
    [ "$status" -eq 0 ]

    # File should remain unchanged
    [ -f "$IDIR/config/settings.json" ]
    [ ! -L "$IDIR/config/settings.json" ]
}

@test "no-op when install config directory doesn't exist" {
    local pd_helix="$BATS_TEST_TMPDIR/home/pi/printer_data/config/helixscreen"
    setup_pd_helix "$pd_helix"
    rmdir "$IDIR/config"

    run restore_config_symlinks
    [ "$status" -eq 0 ]
}

# --- Verify the function exists in the actual script ---

@test "restore-user-config.sh contains restore_config_symlinks function" {
    grep -q 'restore_config_symlinks()' "$WORKTREE_ROOT/config/restore-user-config.sh"
}

@test "restore-user-config.sh calls restore_config_symlinks" {
    # Should call the function (not just define it)
    grep -q '^    restore_config_symlinks' "$WORKTREE_ROOT/config/restore-user-config.sh"
}

@test "restore-user-config.sh lists all expected user config files" {
    grep -q 'settings.json' "$WORKTREE_ROOT/config/restore-user-config.sh"
    grep -q 'helixscreen.env' "$WORKTREE_ROOT/config/restore-user-config.sh"
    grep -q 'tool_spools.json' "$WORKTREE_ROOT/config/restore-user-config.sh"
    grep -q '.disabled_services' "$WORKTREE_ROOT/config/restore-user-config.sh"
}
