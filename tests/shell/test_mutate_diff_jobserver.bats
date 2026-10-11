#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# mutate_diff.py rebuilds once per mutant. Inside a jobserver (a test host's jobpool,
# joined by test-host-run.sh) a -j on make's command line would pull the build out
# of the pool, so build() passes --jobs as -j only when no jobserver is set.

load helpers

SCRIPT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)/scripts/mutate_diff.py"

setup() {
    mock_command_script make 'echo "make $*" >> "$BATS_TEST_TMPDIR/make.log"'
}

build_once() {
    # The make binary is injected: these tests pin the -j handling through the
    # `make` PATH mock above, and make_bin()'s own probe would sidestep the
    # mock on any host that also has a gmake.
    python3 -c '
import importlib.util, io, sys
spec = importlib.util.spec_from_file_location("mutate_diff", sys.argv[1])
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
ok, _ = m.build(sys.argv[2], 7, io.StringIO(), make="make")
sys.exit(0 if ok else 1)' "$SCRIPT" "$BATS_TEST_TMPDIR"
}

@test "with no jobserver the build takes --jobs as -j" {
    unset MAKEFLAGS
    run build_once
    [ "$status" -eq 0 ]
    [ "$(cat "$BATS_TEST_TMPDIR/make.log")" = "make -j7 test-build" ]
}

@test "inside a jobserver the build passes no -j" {
    MAKEFLAGS=" -j --jobserver-auth=3,4" run build_once
    [ "$status" -eq 0 ]
    [ "$(cat "$BATS_TEST_TMPDIR/make.log")" = "make test-build" ]
}
