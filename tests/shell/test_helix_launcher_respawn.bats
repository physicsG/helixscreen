#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for helix-launcher.sh boot-time respawn self-heal.
#
# Background: some firmwares (notably the Snapmaker U1 on Paxx Extended 1.4)
# send helix-screen a single SIGTERM during the busy boot sequence. helix-screen
# handles SIGTERM with a fast _exit(0) (see graceful_quit_signal_handler),
# expecting a supervisor to respawn it. The U1 ships NO helix-watchdog and
# busybox SysV init does not respawn S99 children, so that one signal is
# permanent — and because helix-screen owns the WiFi association on the U1, its
# death leaves the device dark AND off-network (unreachable).
#
# The launcher therefore self-heals: if helix-screen exits very quickly after
# launch (uptime < window — i.e. it never became interactive), it is respawned
# up to HELIX_BOOT_RESPAWN_MAX times. A clean exit AFTER a normal run (uptime >=
# window) is treated as a deliberate quit and is NOT respawned. The feature is
# OFF by default (MAX=0) so supervised platforms are unchanged; the U1 platform
# hook opts in.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"
LAUNCHER="$WORKTREE_ROOT/scripts/helix-launcher.sh"

setup() {
    load helpers

    # Mock the system commands the launcher pokes at startup so the e2e runs
    # don't touch the real dev machine (display-sleep.service, killall, etc.).
    mock_command_script "systemctl" 'exit 0'
    mock_command_script "killall" 'exit 0'
    mock_command_script "setterm" 'exit 0'

    # No DRM card, so the launcher never runs helix-screen --probe-egl and
    # every launch counted is a real one, whatever /dev/dri this host has.
    export HELIX_DRI_DIR="$BATS_TEST_TMPDIR/dri"
    mkdir -p "$HELIX_DRI_DIR"

    export MOCK_INSTALL="$BATS_TEST_TMPDIR/helixscreen"
    mkdir -p "$MOCK_INSTALL/bin" "$MOCK_INSTALL/config"

    # Splash present but inert; no watchdog (U1 ships none → launcher runs
    # helix-screen directly, which is the path the respawn loop must cover).
    printf '#!/bin/sh\nexit 0\n' > "$MOCK_INSTALL/bin/helix-splash"
    chmod +x "$MOCK_INSTALL/bin/helix-splash"

    cp "$LAUNCHER" "$MOCK_INSTALL/bin/helix-launcher.sh"
}

# Install a mock helix-screen that records each launch and fast-exits 0,
# simulating the boot-time SIGTERM that helix turns into _exit(0).
install_fastexit_helix() {
    cat > "$MOCK_INSTALL/bin/helix-screen" <<EOF
#!/bin/sh
echo launch >> "$MOCK_INSTALL/launch_count.txt"
exit 0
EOF
    chmod +x "$MOCK_INSTALL/bin/helix-screen"
    rm -f "$MOCK_INSTALL/launch_count.txt"
}

launch_count() {
    [ -f "$MOCK_INSTALL/launch_count.txt" ] && wc -l < "$MOCK_INSTALL/launch_count.txt" | tr -d ' ' || echo 0
}

@test "launcher has valid sh syntax (with respawn loop)" {
    sh -n "$LAUNCHER"
}

@test "respawn: fast boot-time exit is retried up to HELIX_BOOT_RESPAWN_MAX" {
    install_fastexit_helix

    # MAX=3, generous window so the instant exit always counts as a boot-kill,
    # zero delay so the test doesn't sleep.
    HELIX_BOOT_RESPAWN_MAX=3 HELIX_BOOT_RESPAWN_WINDOW=600 HELIX_BOOT_RESPAWN_DELAY=0 \
        MOCK_INSTALL="$MOCK_INSTALL" sh "$MOCK_INSTALL/bin/helix-launcher.sh" 2>/dev/null || true

    # 1 initial launch + 3 respawns, then it gives up.
    [ "$(launch_count)" -eq 4 ]
}

@test "respawn: disabled by default (no boot-respawn env) — single launch" {
    install_fastexit_helix

    MOCK_INSTALL="$MOCK_INSTALL" sh "$MOCK_INSTALL/bin/helix-launcher.sh" 2>/dev/null || true

    [ "$(launch_count)" -eq 1 ]
}

@test "respawn: a stable run that exits after the window is NOT respawned" {
    # Mock runs longer than the window, then exits 0 — a deliberate quit, not a
    # boot-time kill. Must NOT be respawned even though MAX is high.
    cat > "$MOCK_INSTALL/bin/helix-screen" <<EOF
#!/bin/sh
echo launch >> "$MOCK_INSTALL/launch_count.txt"
sleep 2
exit 0
EOF
    chmod +x "$MOCK_INSTALL/bin/helix-screen"
    rm -f "$MOCK_INSTALL/launch_count.txt"

    HELIX_BOOT_RESPAWN_MAX=3 HELIX_BOOT_RESPAWN_WINDOW=1 HELIX_BOOT_RESPAWN_DELAY=0 \
        MOCK_INSTALL="$MOCK_INSTALL" sh "$MOCK_INSTALL/bin/helix-launcher.sh" 2>/dev/null || true

    [ "$(launch_count)" -eq 1 ]
}

@test "respawn: loop is bounded — never exceeds MAX+1 launches" {
    install_fastexit_helix

    HELIX_BOOT_RESPAWN_MAX=2 HELIX_BOOT_RESPAWN_WINDOW=600 HELIX_BOOT_RESPAWN_DELAY=0 \
        MOCK_INSTALL="$MOCK_INSTALL" sh "$MOCK_INSTALL/bin/helix-launcher.sh" 2>/dev/null || true

    [ "$(launch_count)" -eq 3 ]
}
