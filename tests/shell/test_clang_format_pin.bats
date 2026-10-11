#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/quality-checks.sh formats C++ with exactly one binary: the
# clang-format wheel pinned in requirements.txt. Any other formatter, even a
# neighbouring 18.x, reflows differently, and two formatters taking turns on a
# file is how it ping-pongs between commits. These cases pin the resolver that
# enforces that: the venv wheel or a CLANG_FORMAT override, and only at the
# pinned version.

load helpers

setup() {
  cd "$BATS_TEST_DIRNAME/../.." || return 1
  FAKE_ROOT="$BATS_TEST_TMPDIR/root"
  mkdir -p "$FAKE_ROOT/.venv/bin"
  echo "clang-format==18.1.8" > "$FAKE_ROOT/requirements.txt"
  export FAKE_ROOT
}

# Writes an executable at $1 that reports clang-format version $2.
fake_clang_format() {
  printf '#!/bin/sh\necho "clang-format version %s"\n' "$2" > "$1"
  chmod +x "$1"
}

# Runs the real resolver with REPO_ROOT pointed at the fake tree and prints
# its verdict, the resolved binary and the error text.
resolve() {
  bash -c '
    REPO_ROOT="$1"
    . scripts/qc/_lib.sh
    . scripts/qc/phase2.sh
    if qc_resolve_clang_format; then echo "resolved: $CF_BIN"; else echo "unresolved: $CF_RESOLVE_ERR"; fi
  ' _ "$FAKE_ROOT"
}

@test "no venv wheel resolves nothing and names make venv-setup as the fix" {
  run resolve
  [ "$status" -eq 0 ]
  contains "unresolved:" "$output"
  contains "18.1.8" "$output"
  run grep -n 'make venv-setup' scripts/qc/phase2.sh
  [ "$status" -eq 0 ]
}

@test "a venv wheel at the pinned version resolves" {
  fake_clang_format "$FAKE_ROOT/.venv/bin/clang-format" 18.1.8
  run resolve
  contains "resolved: $FAKE_ROOT/.venv/bin/clang-format" "$output"
}

@test "a venv clang-format at another 18.x does not resolve" {
  fake_clang_format "$FAKE_ROOT/.venv/bin/clang-format" 18.1.3
  run resolve
  contains "unresolved:" "$output"
  contains "18.1.3" "$output"
  contains "18.1.8" "$output"
}

@test "CLANG_FORMAT overrides the venv only at the pinned version" {
  fake_clang_format "$FAKE_ROOT/.venv/bin/clang-format" 18.1.8
  fake_clang_format "$FAKE_ROOT/other-cf" 18.1.8
  CLANG_FORMAT="$FAKE_ROOT/other-cf" run resolve
  contains "resolved: $FAKE_ROOT/other-cf" "$output"
  fake_clang_format "$FAKE_ROOT/other-cf" 23.1.0
  CLANG_FORMAT="$FAKE_ROOT/other-cf" run resolve
  contains "unresolved:" "$output"
  contains "23.1.0" "$output"
}

@test "nothing on PATH is ever consulted" {
  # A PATH clang-format at the pinned version must not rescue a missing wheel.
  mock_command_script "clang-format" 'echo "clang-format version 18.1.8"'
  mock_command_script "clang-format-18" 'echo "clang-format version 18.1.8"'
  run resolve
  contains "unresolved:" "$output"
  run bash -c "cat scripts/quality-checks.sh scripts/qc/*.sh | grep -c 'clang-format-18'"
  [ "$output" = "0" ] || fail "the quality gate still names a PATH fallback"
}
