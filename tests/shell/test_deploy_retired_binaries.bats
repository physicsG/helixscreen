#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# helix-screen carries the EGL and fbdev rungs itself, so the separate
# helix-screen-egl and helix-screen-fbdev an older release or deploy left in
# bin/ must go. A release install never leaves them: every update path
# replaces bin/ wholesale. A dev deploy rsyncs into the existing tree, so
# deploy-common removes them, which these tests pin.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"
CROSS_MK="$WORKTREE_ROOT/mk/cross.mk"

setup() {
    load helpers
}

define_body() {
    awk -v d="^define $1[ \t]*$" '
        $0 ~ d { inside = 1; next }
        inside && /^endef/ { exit }
        inside { print }
    ' "$CROSS_MK"
}

@test "deploy-remove-retired-binaries removes both retired binaries on the device" {
    local dir="$BATS_TEST_TMPDIR/frag"
    mkdir -p "$dir"
    mock_command_script "ssh" "echo \"\$*\" >> \"$BATS_TEST_TMPDIR/ssh.log\""
    {
        printf 'define deploy-remove-retired-binaries\n'
        define_body deploy-remove-retired-binaries
        printf 'endef\n'
        printf 'run:\n\t$(call deploy-remove-retired-binaries,printer,/opt/helixscreen)\n'
    } > "$dir/Makefile"

    run make --no-print-directory -C "$dir" -f Makefile run
    [ "$status" -eq 0 ] || fail "$output"
    run cat "$BATS_TEST_TMPDIR/ssh.log"
    contains "printer" "$output"
    contains "rm -f /opt/helixscreen/bin/helix-screen-egl" "$output"
    contains "/opt/helixscreen/bin/helix-screen-fbdev" "$output"
}

@test "deploy-common removes the retired binaries" {
    define_body deploy-common | grep -q 'call deploy-remove-retired-binaries,$(1),$(2)'
}

@test "the installer no longer falls back to a helix-screen-fbdev binary" {
    run grep -n "helix-screen-fbdev" "$WORKTREE_ROOT/scripts/lib/installer/requirements.sh"
    [ "$status" -ne 0 ] || fail "requirements.sh still names helix-screen-fbdev: $output"
}
