#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Meta-tests for the commit-time ESP32 compile check, scripts/qc/esp32_compile.sh
# driving `scripts/esp32_syntax_check.py --staged`.
#
# The compile itself runs in the espressif/idf image, which no shell test may
# start; unit selection is pinned in tests/python/test_esp32_syntax_check.py.
# What is pinned here is the hook's side of the contract: firmware source and
# header paths reach the gate and nothing else does, the compile reads the
# STAGED copy of each file, a failing compile fails the commit, the full sweep
# leaves the check to pre-push, a commit touching no source starts no
# container, and the hook never pays for a firmware configure. The docker stub
# stands in for the IDF container: it reads the job file the script hands it
# and reports a unit as failing when the source it was told to compile says BAD.

load helpers

REPO_ROOT="$(cd "${BATS_TEST_DIRNAME}/../.." && pwd)"

setup() {
    DOCKER_LOG="$BATS_TEST_TMPDIR/docker.log"
    export DOCKER_LOG
    mkdir -p "$BATS_TEST_TMPDIR/bin"
    # Answers `image inspect` as present, records every call, and plays the
    # container for `run`.
    cat > "$BATS_TEST_TMPDIR/bin/docker" <<STUB
#!/bin/sh
echo "\$*" >> "$DOCKER_LOG"
[ "\$1 \$2" = "image inspect" ] && exit 0
[ "\$1" = run ] || exit 1
for last; do :; done
jobs="\$(printf '%s' "\$last" | sed -n 's/.*--run-jobs \\([^ ]*\\).*/\\1/p')"
exec python3 "$BATS_TEST_TMPDIR/bin/fake_idf.py" "\$jobs"
STUB
    cat > "$BATS_TEST_TMPDIR/bin/fake_idf.py" <<'PY'
import json, sys
failed = []
for spec in json.load(open(sys.argv[1])):
    src = next(a for a in spec["args"] if a.endswith(".cpp"))
    if "BAD" in open(src).read():
        failed.append({"rel": spec["rel"], "out": f"{src}:1: error: BAD does not compile"})
print(json.dumps(failed))
PY
    chmod +x "$BATS_TEST_TMPDIR/bin/docker"
    export PATH="$BATS_TEST_TMPDIR/bin:$PATH"
    export XDG_CACHE_HOME="$BATS_TEST_TMPDIR/cache"

    FIX="$BATS_TEST_TMPDIR/fw"
    mkdir -p "$FIX/src/printer" "$FIX/firmware/helixscreen-esp32"
    cd "$FIX" || return 1
    git init -q .
    git config user.email t@example.com
    git config user.name tester
    printf 'int a() { return 1; }\n' > src/printer/a.cpp
    printf 'cmake_minimum_required(VERSION 3.16)\n' > firmware/helixscreen-esp32/CMakeLists.txt
    git add -A
    git commit -qm base --no-verify
}

# Cached firmware flags for the fixture's CMake state, as a configure leaves them.
seed_flags() {
    local key cache
    key="$(python3 -c 'import importlib.util,sys
spec = importlib.util.spec_from_file_location("e", sys.argv[1]); m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m); print(m.config_key(sys.argv[2], "HEAD"))' \
        "$REPO_ROOT/scripts/esp32_syntax_check.py" "$FIX")"
    cache="$XDG_CACHE_HOME/helixscreen/esp32-syntax/$key"
    mkdir -p "$cache"
    printf '/cfgroot\n' > "$cache/source_root.txt"
    printf '[{"directory": "%s", "file": "/cfgroot/src/printer/a.cpp", "command": "c++ -I/cfgroot/include -c /cfgroot/src/printer/a.cpp"}]\n' \
        "$FIX" > "$cache/compile_commands.json"
}

run_gate() {
    run bash -c '
        STAGED_ONLY="$1"; QC_TMP="$(mktemp -d)"; QC_COUNTS="$QC_TMP/counts"
        . "$2/scripts/qc/_lib.sh"
        . "$2/scripts/qc/esp32_compile.sh"
        cd "$3" && mkdir -p scripts && ln -sf "$2/scripts/esp32_syntax_check.py" scripts/
        qc_esp32_compile' _ "$1" "$REPO_ROOT" "$FIX"
}

@test "the trigger takes firmware sources and headers and nothing else" {
    cd "$REPO_ROOT" || return 1
    re="$(qc_trigger esp32_compile)"
    [ -n "$re" ]
    for p in src/printer/ams_state.cpp include/printer_state.h src/lib.c \
             firmware/helixscreen-esp32/main/main.cpp; do
        echo "$p" | grep -qE "$re" || fail "$p should trigger"
    done
    for p in ui_xml/globals.xml docs/devel/README.md tests/unit/test_x.cpp scripts/foo.py \
             src/printer/notes.md; do
        if echo "$p" | grep -qE "$re"; then fail "$p should not trigger"; fi
    done
}

@test "a staged firmware source with no cached flags is left to pre-push, never configured" {
    printf 'int a() { return 2; }\n' > src/printer/a.cpp
    git add src/printer/a.cpp
    run_gate true
    [ "$status" -eq 0 ]
    contains "no cached firmware flags" "$output"
    contains "pre-push configures them" "$output"
    run grep -c '^run' "$DOCKER_LOG"
    [ "$output" = "0" ] || fail "the hook started a container: $(cat "$DOCKER_LOG")"
}

@test "a commit staging no source or header starts nothing and prints nothing" {
    printf 'notes\n' > README.md
    git add README.md
    run_gate true
    [ "$status" -eq 0 ]
    [ -z "$output" ]
    [ ! -s "$DOCKER_LOG" ]
}

@test "the full sweep leaves the check to pre-push" {
    printf 'int a() { return 2; }\n' > src/printer/a.cpp
    git add src/printer/a.cpp
    run_gate false
    [ "$status" -eq 0 ]
    [ -z "$output" ]
    [ ! -s "$DOCKER_LOG" ]
}

@test "HELIX_ESP32_SYNTAX=0 skips it" {
    printf 'int a() { return 2; }\n' > src/printer/a.cpp
    git add src/printer/a.cpp
    HELIX_ESP32_SYNTAX=0 run_gate true
    [ "$status" -eq 0 ]
    [ -z "$output" ]
}

@test "a staged unit that does not compile fails the commit and prints the error" {
    seed_flags
    printf 'int a() { BAD }\n' > src/printer/a.cpp
    git add src/printer/a.cpp
    run_gate true
    [ "$status" -eq 1 ]
    contains "src/printer/a.cpp does not compile for the ESP32" "$output"
    contains "error: BAD does not compile" "$output"
    contains "--staged" "$output"
}

@test "the staged copy is compiled, not a working-tree fix the commit lacks" {
    seed_flags
    printf 'int a() { BAD }\n' > src/printer/a.cpp
    git add src/printer/a.cpp
    printf 'int a() { return 3; }\n' > src/printer/a.cpp
    run_gate true
    [ "$status" -eq 1 ]
    contains "does not compile for the ESP32" "$output"
}

@test "a peer's unstaged breakage in the working tree does not fail a good commit" {
    seed_flags
    printf 'int a() { return 3; }\n' > src/printer/a.cpp
    git add src/printer/a.cpp
    printf 'int a() { BAD }\n' > src/printer/a.cpp
    run_gate true
    [ "$status" -eq 0 ]
    contains "1 firmware unit(s) compile" "$output"
    run find "$XDG_CACHE_HOME" -name 'index-*'
    [ -z "$output" ] || fail "the index overlay was left behind: $output"
}
