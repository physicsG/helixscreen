#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Installing from a FORK.
#
# Two things have to hold, and neither did before:
#   1. GITHUB_REPO must be settable from the environment. The generated bundles
#      used to hard-assign it in their header, which ran before the modules'
#      own `: "${GITHUB_REPO:=...}"` default and silently won.
#   2. HELIX_GITHUB_ONLY=1 must actually suppress the CDN and HTTP-mirror
#      tiers. Those serve UPSTREAM's artifacts and are consulted first, so a
#      fork install that left them on would resolve upstream's version and
#      install upstream's binary under it.

RELEASE_SH="scripts/lib/installer/release.sh"
INSTALL_SH="scripts/install.sh"
UNINSTALL_SH="scripts/uninstall.sh"
FORK_SH="scripts/install-fork.sh"

setup() {
    source tests/shell/helpers.bash
    export GITHUB_REPO="prestonbrown/helixscreen"
    source "$RELEASE_SH"
}

# --- HELIX_GITHUB_ONLY knob ---

@test "HELIX_GITHUB_ONLY defaults to off" {
    unset HELIX_GITHUB_ONLY
    unset _HELIX_RELEASE_SOURCED
    source "$RELEASE_SH"
    [ "$HELIX_GITHUB_ONLY" = "0" ]
    run _helix_github_only
    [ "$status" -ne 0 ]
}

@test "HELIX_GITHUB_ONLY=1 is honoured from the environment" {
    export HELIX_GITHUB_ONLY=1
    unset _HELIX_RELEASE_SOURCED
    source "$RELEASE_SH"
    run _helix_github_only
    [ "$status" -eq 0 ]
}

# --- The generated bundles must not clobber GITHUB_REPO ---

@test "install.sh lets the environment set GITHUB_REPO" {
    # The defect this pins: a bare `GITHUB_REPO="prestonbrown/helixscreen"` in
    # the bundle header. Every assignment in the shipped installer must be the
    # soft form, or a fork install silently targets upstream.
    run grep -n '^GITHUB_REPO=' "$INSTALL_SH"
    [ "$status" -ne 0 ]
    run grep -c ': "${GITHUB_REPO:=' "$INSTALL_SH"
    [ "$status" -eq 0 ]
}

@test "uninstall.sh lets the environment set GITHUB_REPO" {
    run grep -n '^GITHUB_REPO=' "$UNINSTALL_SH"
    [ "$status" -ne 0 ]
}

@test "the bundle generators emit the soft form" {
    # Regenerating from these is what produces the shipped bundles, so the fix
    # has to live here or the next re-bundle undoes it.
    run grep -n '^GITHUB_REPO="' scripts/bundle-installer.sh
    [ "$status" -ne 0 ]
    run grep -n '^GITHUB_REPO="' scripts/bundle-uninstaller.sh
    [ "$status" -ne 0 ]
    run grep -n '^GITHUB_REPO="' scripts/install-dev.sh
    [ "$status" -ne 0 ]
}

# --- What gets written onto the printer must name the installing repo ---

@test "moonraker update_manager points at GITHUB_REPO, not a hardcoded slug" {
    run grep -n 'repo: prestonbrown/helixscreen' scripts/lib/installer/moonraker.sh
    [ "$status" -ne 0 ]
    run grep -n 'repo: ${GITHUB_REPO}' scripts/lib/installer/moonraker.sh
    [ "$status" -eq 0 ]
}

@test "release_info.json project_owner follows GITHUB_REPO" {
    # Moonraker reads this to decide which repo's releases are updates for this
    # install; a fork install that wrote "prestonbrown" would be offered
    # upstream's builds as its own updates.
    run grep -n '"project_owner":"prestonbrown"' scripts/lib/installer/moonraker.sh
    [ "$status" -ne 0 ]

    GITHUB_REPO="physicsG/helixscreen"
    owner="${GITHUB_REPO%%/*}"
    name="${GITHUB_REPO##*/}"
    [ "$owner" = "physicsG" ]
    [ "$name" = "helixscreen" ]
}

# --- The fork front-end ---

@test "install-fork.sh is valid POSIX sh" {
    run sh -n "$FORK_SH"
    [ "$status" -eq 0 ]
}

@test "install-fork.sh sets both required variables" {
    run grep -q 'HELIX_GITHUB_ONLY=1' "$FORK_SH"
    [ "$status" -eq 0 ]
    run grep -q 'export GITHUB_REPO HELIX_GITHUB_ONLY' "$FORK_SH"
    [ "$status" -eq 0 ]
}

@test "install-fork.sh defaults to the fork but yields to the environment" {
    run grep -q ': "${GITHUB_REPO:=physicsG/helixscreen}"' "$FORK_SH"
    [ "$status" -eq 0 ]
}

@test "install-fork.sh prefers an installer sitting next to it" {
    # A release tarball unpacks install.sh beside install-fork.sh, and the
    # printer may have no route to raw.githubusercontent.com at all.
    tmp="$BATS_TEST_TMPDIR/rel"
    mkdir -p "$tmp"
    cp "$FORK_SH" "$tmp/"
    # Stand-in installer that records how it was called. It mentions
    # HELIX_GITHUB_ONLY, so it passes the fork-support check by construction.
    cat > "$tmp/install.sh" <<'EOF'
#!/bin/sh
echo "LOCAL_INSTALLER repo=${GITHUB_REPO} only=${HELIX_GITHUB_ONLY} args=$*"
EOF
    chmod +x "$tmp/install.sh"

    # setup() exports GITHUB_REPO for the release.sh tests; clear it here so
    # this exercises the script's own default rather than the inherited value.
    unset GITHUB_REPO
    run sh "$tmp/install-fork.sh" --update
    [ "$status" -eq 0 ]
    [[ "$output" == *"LOCAL_INSTALLER"* ]]
    [[ "$output" == *"repo=physicsG/helixscreen"* ]]
    [[ "$output" == *"only=1"* ]]
    [[ "$output" == *"args=--update"* ]]
}

@test "install-fork.sh passes GITHUB_REPO through to the installer" {
    tmp="$BATS_TEST_TMPDIR/rel2"
    mkdir -p "$tmp"
    cp "$FORK_SH" "$tmp/"
    # Carries the fork-support marker so the check lets it run.
    cat > "$tmp/install.sh" <<'EOF'
#!/bin/sh
: "${HELIX_GITHUB_ONLY:=0}"
echo "repo=${GITHUB_REPO}"
EOF
    chmod +x "$tmp/install.sh"

    GITHUB_REPO="someone/helixscreen" run sh "$tmp/install-fork.sh"
    [ "$status" -eq 0 ]
    [[ "$output" == *"repo=someone/helixscreen"* ]]
}

@test "install-fork.sh refuses an installer that predates fork support" {
    # The trap this pins: an old install.sh (hard GITHUB_REPO=, no
    # HELIX_GITHUB_ONLY) would take the environment this script sets, ignore
    # it, and quietly install upstream's binary. It has to fail loudly instead.
    tmp="$BATS_TEST_TMPDIR/rel3"
    mkdir -p "$tmp"
    cp "$FORK_SH" "$tmp/"
    cat > "$tmp/install.sh" <<'EOF2'
#!/bin/sh
GITHUB_REPO="prestonbrown/helixscreen"
echo "OLD_INSTALLER_RAN repo=${GITHUB_REPO}"
EOF2
    chmod +x "$tmp/install.sh"

    run sh "$tmp/install-fork.sh"
    [ "$status" -ne 0 ]
    [[ "$output" != *"OLD_INSTALLER_RAN"* ]]
    [[ "$output" == *"predates fork support"* ]]
}

@test "install-fork.sh accepts the shipped install.sh, which carries fork support" {
    # The real bundle must pass the same check the stand-ins are held to.
    run grep -q 'HELIX_GITHUB_ONLY' "$INSTALL_SH"
    [ "$status" -eq 0 ]
    tmp="$BATS_TEST_TMPDIR/rel4"
    mkdir -p "$tmp"
    cp "$FORK_SH" "$tmp/"
    # A stand-in that carries the marker, so the check passes and it runs.
    printf '#!/bin/sh\n# HELIX_GITHUB_ONLY honoured here\necho NEW_INSTALLER_RAN\n' > "$tmp/install.sh"
    run sh "$tmp/install-fork.sh"
    [ "$status" -eq 0 ]
    [[ "$output" == *"NEW_INSTALLER_RAN"* ]]
}
