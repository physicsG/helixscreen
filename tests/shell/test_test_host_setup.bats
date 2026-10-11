#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/test-host-setup.sh refuses a layout the container could not use,
# before it reaches the host: the container mounts the parent of the mirror
# root, so the root needs a parent of its own, the same name on both sides,
# and everything else the container keeps must sit under that mount.

SETUP="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)/scripts/test-host-setup.sh"

setup() {
    load helpers
    export HELIX_TEST_HOST=testhost.invalid
    mock_command_script ssh 'touch "$BATS_TEST_TMPDIR/ssh-called"; exit 255'
}

refused() { # <expected text>
    [ "$status" -ne 0 ]
    contains "$1" "$output"
    [ ! -e "$BATS_TEST_TMPDIR/ssh-called" ]
}

@test "a mirror root with no parent of its own is refused: it would mount the whole home" {
    HELIX_TEST_TREES_HOST=trees run "$SETUP"
    refused "needs a parent directory of its own"
}

@test "a mirror root named differently on the host and in the container is refused" {
    HELIX_TEST_TREES_HOST=/srv/mirrors HELIX_TEST_TREES=/work/trees run "$SETUP"
    refused "must end in the same directory name"
}

@test "a ccache or checkout outside the mounted directory is refused" {
    HELIX_TEST_CCACHE=/var/ccache run "$SETUP"
    refused "/var/ccache is not under /work"
    HELIX_TEST_WORKDIR=/src/helixscreen run "$SETUP"
    refused "/src/helixscreen is not under /work"
}

@test "with the default layout it goes on to ask the host" {
    run "$SETUP"
    [ "$status" -ne 0 ]
    [ -e "$BATS_TEST_TMPDIR/ssh-called" ]
    contains "needs Docker and passwordless sudo" "$output"
}
