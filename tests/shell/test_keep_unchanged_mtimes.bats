#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# libhv's `make libhv` rewrites include/hv/* on every run, and json.hpp there is
# a prerequisite of every C++ object. scripts/keep-unchanged-mtimes.sh wraps that
# run so a header whose bytes did not change keeps its mtime; mk/deps.mk must
# route every libhv sub-make through it, and keep a dry run dry.

bats_require_minimum_version 1.5.0
load helpers

REPO="$(cd "${BATS_TEST_DIRNAME}/../.." && pwd)"
KEEP="$REPO/scripts/keep-unchanged-mtimes.sh"
DEPS="$REPO/mk/deps.mk"

setup() {
    unset MAKEFLAGS MFLAGS MAKELEVEL
    SRC="$BATS_TEST_TMPDIR/src"
    OUT="$BATS_TEST_TMPDIR/include/hv"
    mkdir -p "$SRC"
    printf 'json v1\n' > "$SRC/json.hpp"
    printf 'hconfig\n' > "$SRC/hconfig.h"
    # The same unconditional copy libhv's Makefile does.
    GEN="$BATS_TEST_TMPDIR/gen.sh"
    cat > "$GEN" <<EOF
#!/bin/sh
mkdir -p "$OUT"
cp -r "$SRC"/* "$OUT"
EOF
    chmod +x "$GEN"
}

mtime() { stat -c %Y "$1"; }

@test "a second generation with unchanged content keeps every header's mtime" {
    run "$KEEP" "$OUT" -- "$GEN"
    [ "$status" -eq 0 ]
    touch -d '2020-01-01 00:00:00' "$OUT/json.hpp" "$OUT/hconfig.h"
    before_json="$(mtime "$OUT/json.hpp")"
    before_cfg="$(mtime "$OUT/hconfig.h")"

    run "$KEEP" "$OUT" -- "$GEN"
    [ "$status" -eq 0 ]
    [ "$(mtime "$OUT/json.hpp")" -eq "$before_json" ]
    [ "$(mtime "$OUT/hconfig.h")" -eq "$before_cfg" ]
}

@test "the unwrapped generator does move the mtime (the test can see a rewrite)" {
    "$GEN"
    touch -d '2020-01-01 00:00:00' "$OUT/json.hpp"
    before="$(mtime "$OUT/json.hpp")"
    "$GEN"
    [ "$(mtime "$OUT/json.hpp")" -gt "$before" ]
}

@test "a header whose content changed gets the new mtime, its siblings do not" {
    "$GEN"
    touch -d '2020-01-01 00:00:00' "$OUT/json.hpp" "$OUT/hconfig.h"
    before_cfg="$(mtime "$OUT/hconfig.h")"
    printf 'json v2\n' > "$SRC/json.hpp"

    run "$KEEP" "$OUT" -- "$GEN"
    [ "$status" -eq 0 ]
    [ "$(cat "$OUT/json.hpp")" = "json v2" ]
    [ "$(mtime "$OUT/json.hpp")" -gt "$before_cfg" ]
    [ "$(mtime "$OUT/hconfig.h")" -eq "$before_cfg" ]
}

@test "a missing output directory is created by the command and left alone" {
    run "$KEEP" "$OUT" -- "$GEN"
    [ "$status" -eq 0 ]
    [ -f "$OUT/json.hpp" ]
}

@test "the command's exit status is returned, and unchanged files are still restored" {
    "$GEN"
    touch -d '2020-01-01 00:00:00' "$OUT/json.hpp"
    before="$(mtime "$OUT/json.hpp")"
    run "$KEEP" "$OUT" -- sh -c "\"$GEN\"; exit 7"
    [ "$status" -eq 7 ]
    [ "$(mtime "$OUT/json.hpp")" -eq "$before" ]
}

@test "usage error without the -- separator" {
    run "$KEEP" "$OUT" "$GEN"
    [ "$status" -eq 2 ]
}

@test "every libhv sub-make in mk/deps.mk runs through the wrapper" {
    run grep -cE '\$\(MAKE\).*-C \$\(LIBHV_DIR\).* libhv(;|$)' "$DEPS"
    [ "$output" -ge 3 ]
    total="$output"
    run grep -cE '\$\(LIBHV_KEEP_MTIMES\).*\$\(MAKE\).*-C \$\(LIBHV_DIR\).* libhv(;|$)' "$DEPS"
    [ "$output" -eq "$total" ]
    run grep -E '^LIBHV_KEEP_MTIMES :?= scripts/keep-unchanged-mtimes.sh \$\(LIBHV_DIR\)/include/hv --$' "$DEPS"
    [ "$status" -eq 0 ]
}

@test "a libhv sub-make started with a cleared MAKEFLAGS keeps -n" {
    run grep -cE 'MAKEFLAGS= .*\$\(MAKE\)' "$DEPS"
    [ "$output" -ge 1 ]
    total="$output"
    run grep -cE 'MAKEFLAGS= .*\$\(MAKE\).*\$\(LIBHV_DRY_RUN\)' "$DEPS"
    [ "$output" -eq "$total" ]
}

@test "LIBHV_DRY_RUN is -n under make -n and empty otherwise" {
    def="$(grep -E '^LIBHV_DRY_RUN :?= ' "$DEPS")"
    [ -n "$def" ]
    mf="$BATS_TEST_TMPDIR/dry.mk"
    printf '%s\nall:\n\t+@echo "[$(LIBHV_DRY_RUN)]"\n' "$def" > "$mf"

    # Under -n make echoes a `+` line before running it, so read the last line.
    run make --no-print-directory -n -f "$mf"
    [ "${lines[${#lines[@]}-1]}" = "[-n]" ]
    run make --no-print-directory -f "$mf"
    [ "$output" = "[]" ]
    run make --no-print-directory -s -f "$mf"
    [ "$output" = "[]" ]
}
