#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# mk/cross.mk's deploy-common recipe fixes file ownership after rsync (rsync
# preserves the sending host's uid:gid, which on macOS means nothing on the
# device) with a plain `sudo chown`. On a target whose sudo needs a password,
# ssh has no pty to prompt on, sudo fails, and that recipe LINE fails - which
# stops the whole `make deploy-pi` recipe right there, before it ever reaches
# the restart step further down. Every file was already copied; the deploy
# just never restarts the app.
#
# These tests exercise the real deploy-pi recipe with `ssh` and `rsync`
# replaced by loggers on PATH: `chown` and `sudo -n chown` both fail (a device
# with no passwordless sudo), and the assertion is that the recipe still
# reaches and issues the restart command instead of dying on the chown step.
# The second test's mock matches any sudo-and-chown combination, so a bare
# "sudo chown" fails it the same as "sudo -n chown" would.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

setup() {
    load helpers
    cd "$WORKTREE_ROOT" || return 1

    SSH_LOG="$BATS_TEST_TMPDIR/ssh.log"
    : > "$SSH_LOG"
    export SSH_LOG

    # Every ssh call is logged verbatim and answered by pattern, never
    # actually connecting anywhere. Both ownership attempts (plain chown,
    # then sudo -n chown) fail, as they would on a device that owns nothing
    # rsync just wrote and has no passwordless sudo configured.
    mock_command_script "ssh" '
        echo "$*" >> "$SSH_LOG"
        case "$*" in
            *"sudo -n chown"*) exit 1 ;;
            *chown*) exit 1 ;;
            *) exit 0 ;;
        esac
    '
    # rsync never needs to actually run for this recipe's control flow to be
    # exercised, and driving real rsync over the stubbed ssh transport above
    # would just fail the protocol handshake instead of testing anything.
    mock_command_script "rsync" 'echo "$*" >> "$SSH_LOG"; exit 0'

    # deploy-pi's own file-existence guards.
    mkdir -p build/pi/bin
    touch build/pi/bin/helix-screen build/pi/bin/helix-splash

    # deploy-common's image-generation guards: present means skipped, so the
    # recipe never shells out to `make gen-images` et al.
    mkdir -p build/assets/images/prerendered build/assets/images/printers/prerendered
    touch build/assets/images/prerendered/splash-logo-medium.bin
    touch build/assets/images/prerendered/benchy_thumbnail_white.bin
    touch build/assets/images/prerendered/splash-3d-dark-small.bin
    touch build/assets/images/printers/prerendered/dummy.bin
}

teardown() {
    rm -rf build/pi build/assets/images/prerendered build/assets/images/printers/prerendered
}

@test "deploy-pi restarts helix-screen even when sudo chown needs a password" {
    run make deploy-pi PI_HOST=fake-pi-host PI_DEPLOY_DIR=/home/pi/helixscreen

    [ "$status" -eq 0 ]

    # The chown step warned instead of aborting the recipe.
    contains "Could not fix ownership" "$output"

    # And the recipe reached the restart step - the actual bug was that it
    # never got this far.
    grep -qF "sudo -n systemctl start helixscreen" "$SSH_LOG"
}

@test "the ownership fix never invokes sudo when a plain chown already succeeds" {
    mock_command_script "ssh" '
        echo "$*" >> "$SSH_LOG"
        case "$*" in
            # Matches any sudo-and-chown combination, -n or not, so a bare
            # "sudo chown" fails the mock the same as "sudo -n chown" would.
            *sudo*chown*) echo "UNEXPECTED SUDO CALL" >&2; exit 1 ;;
            *) exit 0 ;;
        esac
    '

    run make deploy-pi PI_HOST=fake-pi-host PI_DEPLOY_DIR=/home/pi/helixscreen

    [ "$status" -eq 0 ]
    lacks "Could not fix ownership" "$output"
    ! grep -qE "sudo.*chown" "$SSH_LOG"
    grep -qF "sudo -n systemctl start helixscreen" "$SSH_LOG"
}
