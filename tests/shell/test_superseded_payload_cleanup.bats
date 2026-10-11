#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sweeping the payload install the mod's previous default root left behind.
#
# The payload root lives at the mod_data sibling of the mod's git tree; the
# root it replaced sat at $HOST_MOD_ROOT/.bin/helixscreen, inside that tree.
# An install from the old era left two things on the host: a HOST-side init
# script (a payload install writes its init inside the mod's chroot instead)
# and the tree itself. Left in place, the stale init fires at boot beside the
# chroot's and the two instances fight over the display - and its stop is
# name-based, so stopping either kills both.
#
# The invariant these cases hold: nothing is removed that cannot be
# positively identified as ours. An init is judged by the root its DAEMON_DIR
# names, a tree by the payload it carries, and both only once the replacement
# at INSTALL_DIR is complete.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"
RELEASE_SH="$WORKTREE_ROOT/scripts/lib/installer/release.sh"
SERVICE_SH="$WORKTREE_ROOT/scripts/lib/installer/service.sh"
MAIN_SH="$WORKTREE_ROOT/scripts/lib/installer/main.sh"

setup() {
    load helpers

    unset _HELIX_RELEASE_SOURCED _HELIX_SERVICE_SOURCED
    unset INSTALL_DIR HOST_MOD_ROOT HELIX_MOD_PAYLOAD HELIX_MOD_TREE_CANDIDATES
    export SUDO=""
    # release.sh's download helpers expect a repo; only the cleanup paths run here.
    export GITHUB_REPO="prestonbrown/helixscreen"
    # shellcheck disable=SC1090
    . "$RELEASE_SH"
    # shellcheck disable=SC1090
    . "$SERVICE_SH"

    log_info()    { echo "INFO: $*"; }
    log_warn()    { echo "WARN: $*"; }
    log_error()   { echo "ERROR: $*"; }
    log_success() { echo "OK: $*"; }

    SANDBOX="$BATS_TEST_TMPDIR/root"
    MOD_ROOT="$SANDBOX/opt/config/mod"
    OLD_ROOT="$MOD_ROOT/.bin/helixscreen"
    INSTALL_DIR="$SANDBOX/opt/config/mod_data/helixscreen"
    INITD="$BATS_TEST_TMPDIR/host-init.d"

    mkdir -p "$INITD"
    # The host init.d seam: /etc/init.d is the device's, never the test's.
    HELIX_HOST_INITD_DIR="$INITD"

    HOST_MOD_ROOT="$MOD_ROOT"
    HELIX_MOD_PAYLOAD=1
}

# The replacement at INSTALL_DIR, complete enough to sweep the old root.
seed_verified_root() {
    mkdir -p "$INSTALL_DIR/bin" "$INSTALL_DIR/config"
    printf '#!/bin/sh\nexit 0\n' > "$INSTALL_DIR/bin/helix-screen"
    chmod +x "$INSTALL_DIR/bin/helix-screen"
    printf '{"operator":true}\n' > "$INSTALL_DIR/config/settings.json"
}

# The superseded tree: our payload inside the mod's git tree.
seed_old_tree() {
    mkdir -p "$OLD_ROOT/bin" "$OLD_ROOT/config"
    printf '#!/bin/sh\nexit 0\n' > "$OLD_ROOT/bin/helix-screen"
    chmod +x "$OLD_ROOT/bin/helix-screen"
}

# A host-side init script naming a root, as _set_init_script_daemon_dir writes it.
host_init() {
    printf 'DAEMON_DIR="%s"\n' "$2" > "$INITD/$1"
}

# ---------------------------------------------------------------------------
# The stale init script
# ---------------------------------------------------------------------------

@test "a host init naming the superseded root is removed" {
    seed_verified_root
    seed_old_tree
    host_init S90helixscreen "$OLD_ROOT"

    run cleanup_superseded_payload
    [ "$status" -eq 0 ]
    [ ! -f "$INITD/S90helixscreen" ] || fail "the stale init survived"
    echo "$output" | grep -q "named $OLD_ROOT" || fail "swept it silently: $output"
}

@test "a host init naming the superseded root goes even when its tree is already gone" {
    # The mod's OTA reaps untracked paths inside its tree; the init outlives it.
    seed_verified_root
    host_init S90helixscreen "$OLD_ROOT"

    run cleanup_superseded_payload
    [ "$status" -eq 0 ]
    [ ! -f "$INITD/S90helixscreen" ] || fail "the stale init survived"
}

@test "an init naming the current root is not removed" {
    # The adopted-legacy shape: INSTALL_DIR is the root the host init boots.
    seed_verified_root
    host_init S90helixscreen "$INSTALL_DIR"

    run cleanup_superseded_payload
    [ "$status" -eq 0 ]
    [ -f "$INITD/S90helixscreen" ] || fail "removed this install's own boot path"
}

@test "an init naming an unrelated root is not removed" {
    # The legacy standalone population and dev-deploy roots are not ours.
    seed_verified_root
    seed_old_tree
    host_init S90helixscreen "/opt/helixscreen"
    host_init S91helixscreen "$SANDBOX/home/dev/helixscreen"

    run cleanup_superseded_payload
    [ "$status" -eq 0 ]
    [ -f "$INITD/S90helixscreen" ] || fail "removed the legacy standalone's service"
    [ -f "$INITD/S91helixscreen" ] || fail "removed a dev-deploy's service"
}

@test "an init with no DAEMON_DIR line is not removed" {
    seed_verified_root
    seed_old_tree
    printf '#!/bin/sh\necho not ours to interpret\n' > "$INITD/S90helixscreen"

    run cleanup_superseded_payload
    [ "$status" -eq 0 ]
    [ -f "$INITD/S90helixscreen" ] || fail "removed a script it could not interpret"
}

@test "the alternate mod-tree spelling is recognized" {
    # One mod tree, two host spellings (the AD5X binds both); the init names
    # whichever its install resolved.
    local other="$SANDBOX/usr/data/config/mod"
    HELIX_MOD_TREE_CANDIDATES="$other"
    seed_verified_root
    host_init S90helixscreen "$other/.bin/helixscreen"

    run cleanup_superseded_payload
    [ "$status" -eq 0 ]
    [ ! -f "$INITD/S90helixscreen" ] || fail "the spelling difference hid the stale init"
}

# ---------------------------------------------------------------------------
# The stranded tree
# ---------------------------------------------------------------------------

@test "the old tree goes once the new root is complete" {
    seed_verified_root
    seed_old_tree

    run cleanup_superseded_payload
    [ "$status" -eq 0 ]
    [ ! -d "$OLD_ROOT" ] || fail "the tree left behind was not swept"
}

@test "the old tree stays when the new root has no runnable binary" {
    seed_old_tree
    mkdir -p "$INSTALL_DIR/config"
    printf '{"operator":true}\n' > "$INSTALL_DIR/config/settings.json"

    run cleanup_superseded_payload
    [ "$status" -eq 0 ]
    [ -d "$OLD_ROOT" ] || fail "removed the only working install on the device"
    [ -f "$INITD/S90helixscreen" ] || true
}

@test "the old tree and init stay when the config was not carried" {
    seed_old_tree
    mkdir -p "$INSTALL_DIR/bin"
    printf '#!/bin/sh\nexit 0\n' > "$INSTALL_DIR/bin/helix-screen"
    chmod +x "$INSTALL_DIR/bin/helix-screen"
    host_init S90helixscreen "$OLD_ROOT"

    run cleanup_superseded_payload
    [ "$status" -eq 0 ]
    [ -d "$OLD_ROOT" ] || fail "removed the only copy of the operator's config"
    [ -f "$INITD/S90helixscreen" ] || \
        fail "the init went while its tree was kept: one install, half swept"
}

@test "an unidentifiable directory at the old path is left alone" {
    seed_verified_root
    mkdir -p "$OLD_ROOT/junk"
    printf 'not ours\n' > "$OLD_ROOT/junk/payload.bin"

    run cleanup_superseded_payload
    [ "$status" -eq 0 ]
    [ -d "$OLD_ROOT" ] || fail "removed a directory it could not identify"
}

@test "a non-payload install sweeps nothing" {
    HELIX_MOD_PAYLOAD=""
    seed_verified_root
    seed_old_tree
    host_init S90helixscreen "$OLD_ROOT"

    run cleanup_superseded_payload
    [ "$status" -eq 0 ]
    [ -d "$OLD_ROOT" ] || fail "a standalone install removed a payload tree"
    [ -f "$INITD/S90helixscreen" ] || fail "a standalone install removed a service"
}

@test "installing at the old root supersedes nothing" {
    INSTALL_DIR="$OLD_ROOT"
    seed_old_tree
    host_init S90helixscreen "$OLD_ROOT"

    run cleanup_superseded_payload
    [ "$status" -eq 0 ]
    [ -d "$OLD_ROOT" ] || fail "the install target removed itself"
    [ -f "$INITD/S90helixscreen" ] || fail "removed the boot path of the target root"
}

# ---------------------------------------------------------------------------
# Ordering and wiring
# ---------------------------------------------------------------------------

_step_line() {
    grep -n "^[[:space:]]*$1\b" "$MAIN_SH" | head -1 | cut -d: -f1
}

@test "the superseded payload is swept only after the service is up" {
    local start sweep
    start=$(_step_line start_service)
    sweep=$(_step_line cleanup_superseded_payload)
    [ -n "$start" ] && [ -n "$sweep" ] || fail "step not found: start=$start sweep=$sweep"
    [ "$start" -lt "$sweep" ] || \
        fail "cleanup_superseded_payload (line $sweep) runs before start_service (line $start)"
}

@test "the K2 migration still sweeps through the shared gates" {
    MIGRATE_FROM_DIR="$BATS_TEST_TMPDIR/opt/helixscreen"
    seed_verified_root
    mkdir -p "$MIGRATE_FROM_DIR"
    printf 'x\n' > "$MIGRATE_FROM_DIR/marker"

    run cleanup_migrated_install
    [ "$status" -eq 0 ]
    [ ! -d "$MIGRATE_FROM_DIR" ] || fail "the shared gates lost the K2 sweep"
}
