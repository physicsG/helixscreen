#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The build-hosts file (scripts/lib/build_hosts.sh): one parser, which the
# scripts source and make asks through $(shell). The file names the hosts, the
# environment beats the file on both sides, a line the parser cannot take is
# named and skipped, and a make that uses no remote host never runs the parser,
# so a broken file cannot break an ordinary build.

ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"
LIB="$ROOT/scripts/lib/build_hosts.sh"

setup() {
    load helpers
    cd "$ROOT"
    mock_command_script ssh 'touch "$BATS_TEST_TMPDIR/ssh-called"; exit 255'
    # A makefile that prints what a recipe under mk/remote.mk sees.
    SHOW="$BATS_TEST_TMPDIR/show.mk"
    cat > "$SHOW" <<'MK'
show-remote:
	@echo 'host=[$(REMOTE_HOST)] dir=[$(REMOTE_DIR)] user=[$(REMOTE_USER)]'
show-env:
	@echo "env-test-host=[$$HELIX_TEST_HOST] env-build-dir=[$$REMOTE_BUILD_DIR]"
MK
}

hosts() { printf '%s\n' "$@" > "$HELIX_BUILD_HOSTS_FILE"; }
via_make() { run make -s -f mk/remote.mk -f "$SHOW" "$@"; }
via_shell() { run bash -c '. "$1"; echo "host=[${REMOTE_HOST:-}] dir=[${REMOTE_DIR:-}] user=[${REMOTE_USER:-}]"' _ "$LIB"; }

@test "make and the shell read the same values from the file" {
    hosts REMOTE_HOST=fromfile.invalid REMOTE_DIR=/srv/fromfile
    via_make show-remote
    [ "$status" -eq 0 ]
    contains "host=[fromfile.invalid] dir=[/srv/fromfile]" "$output"
    via_shell
    contains "host=[fromfile.invalid] dir=[/srv/fromfile]" "$output"
}

@test "the environment beats the file, through make and in the shell" {
    hosts REMOTE_HOST=fromfile.invalid REMOTE_DIR=/srv/fromfile
    REMOTE_HOST=fromenv.invalid via_make show-remote
    contains "host=[fromenv.invalid] dir=[/srv/fromfile]" "$output"
    REMOTE_HOST=fromenv.invalid via_shell
    contains "host=[fromenv.invalid] dir=[/srv/fromfile]" "$output"
    # The make command line beats both.
    REMOTE_HOST=fromenv.invalid via_make show-remote REMOTE_HOST=cmdline.invalid
    contains "host=[cmdline.invalid]" "$output"
}

@test "make hands recipes the developer's environment, never the file's value for it" {
    hosts HELIX_TEST_HOST=filehost.invalid REMOTE_BUILD_DIR=/file
    HELIX_TEST_HOST=envhost.invalid REMOTE_BUILD_DIR=/env via_make show-env
    [ "$status" -eq 0 ]
    contains "env-test-host=[envhost.invalid] env-build-dir=[/env]" "$output"
}

@test "a make that uses no remote host never runs the parser, so a broken file cannot stop it" {
    hosts 'HELIX_TEST_HOST zeus' 'export REMOTE_HOST=x' '  REMOTE_DIR=/x'
    run make -s -f mk/remote.mk -f "$SHOW" show-env
    [ "$status" -eq 0 ]
    lacks "build-hosts" "$output"
    lacks "missing separator" "$output"
}

@test "a line the parser cannot take is named and skipped; the good lines still apply" {
    hosts 'HELIX_TEST_HOST zeus' 'REMOTE_HOST=good.invalid' 'export REMOTE_USER=x' '  REMOTE_DIR=/indented'
    via_shell
    contains "host=[good.invalid] dir=[] user=[]" "$output"
    contains ":1: not KEY=VALUE" "$output"
    contains ":3: not KEY=VALUE" "$output"
    contains ":4: not KEY=VALUE" "$output"
    via_make show-remote
    contains "host=[good.invalid] dir=[] user=[]" "$output"
}

@test "values are literal: no \$ expansion, and nothing in the file runs" {
    hosts 'REMOTE_DIR=$HOME/src' 'REMOTE_HOST=$(id)' 'REMOTE_USER=`id`'
    via_shell
    contains 'host=[$(id)] dir=[$HOME/src] user=[`id`]' "$output"
    via_make show-remote
    contains 'host=[$(id)] dir=[$HOME/src] user=[`id`]' "$output"
}

@test "a trailing comment or a quoted value is refused, not half-read" {
    hosts 'REMOTE_HOST=zeus.invalid # the big box' 'REMOTE_DIR="/srv/quoted"'
    via_shell
    contains "host=[] dir=[]" "$output"
    contains ":1: REMOTE_HOST: whitespace in the value" "$output"
    contains ":2: REMOTE_DIR: quotes in the value" "$output"
    via_make show-remote
    contains "host=[] dir=[]" "$output"
}

@test "the last of two lines for one key wins, on both sides" {
    hosts REMOTE_HOST=first.invalid REMOTE_HOST=last.invalid
    via_shell
    contains "host=[last.invalid]" "$output"
    via_make show-remote
    contains "host=[last.invalid]" "$output"
}

@test "a CRLF file reads the same as an LF one" {
    printf 'REMOTE_HOST=crlf.invalid\r\nREMOTE_DIR=/srv/crlf\r\n' > "$HELIX_BUILD_HOSTS_FILE"
    via_shell
    contains "host=[crlf.invalid] dir=[/srv/crlf]" "$output"
    via_make show-remote
    contains "host=[crlf.invalid] dir=[/srv/crlf]" "$output"
}

@test "only the documented keys are ever set" {
    hosts "BASH_ENV=$BATS_TEST_TMPDIR/evil" LD_PRELOAD=/evil.so REMOTE_HOST=ok.invalid
    run bash -c '. "$1"; echo "bash_env=[${BASH_ENV:-}] preload=[${LD_PRELOAD:-}] host=[$REMOTE_HOST]"' _ "$LIB"
    contains "bash_env=[] preload=[] host=[ok.invalid]" "$output"
    contains ":1: BASH_ENV is not a build-hosts key" "$output"
    # The allowlist is the parser's own, and it lists what the docs list.
    run "$LIB" --keys
    [ "$status" -eq 0 ]
    for k in HELIX_TEST_HOST HELIX_TEST_CONTAINER HELIX_TEST_WORKDIR HELIX_TEST_TREES_HOST \
             HELIX_TEST_TREES HELIX_TEST_CCACHE HELIX_TEST_LOCK_DIR HELIX_TEST_HOST_AUTO \
             REMOTE_HOST REMOTE_USER REMOTE_DIR REMOTE_BUILD_DIR; do
        contains "$k" "$output"
    done
}

@test "a file that exists but cannot be read says so" {
    [ "$(id -u)" != 0 ] || skip "root reads a mode-000 file anyway"
    hosts REMOTE_HOST=hidden.invalid
    chmod 000 "$HELIX_BUILD_HOSTS_FILE"
    run scripts/remote-build.sh test
    [ "$status" -ne 0 ]
    contains "cannot read $HELIX_BUILD_HOSTS_FILE" "$output"
}

@test "a remote make target with no host refuses in one line, naming the file" {
    run make -s -f mk/remote.mk remote-ssh
    [ "$status" -ne 0 ]
    [ "${#lines[@]}" -eq 1 ]
    contains "REMOTE_HOST is not set" "$output"
    contains "$HELIX_BUILD_HOSTS_FILE" "$output"
    # The rsync targets need the directory too: --delete into "host:/" is what
    # an empty one would mean.
    REMOTE_HOST=h.invalid run make -s -f mk/remote.mk remote-sync
    [ "$status" -ne 0 ]
    contains "REMOTE_DIR is not set" "$output"
    [ ! -e "$BATS_TEST_TMPDIR/ssh-called" ]
}

@test "remote-build.sh with no host refuses in one line and reaches nothing" {
    run scripts/remote-build.sh test
    [ "$status" -eq 2 ]
    [ "${#lines[@]}" -eq 1 ]
    contains "REMOTE_HOST is not set" "$output"
    contains "$HELIX_BUILD_HOSTS_FILE" "$output"
    [ ! -e "$BATS_TEST_TMPDIR/ssh-called" ]
}
