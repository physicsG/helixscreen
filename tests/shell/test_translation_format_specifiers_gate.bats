#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Meta-tests for scripts/check_translation_format_specifiers.py. A translated
# format string that swaps a placeholder (%d -> %s) crashes or throws at
# runtime (#1073). The gate must see the sink wherever it lives - including
# headers: ams_error.h carries runtime-translated fmt::format sites, and a
# source scan of *.cpp alone is blind to them.

GATE="scripts/check_translation_format_specifiers.py"

setup() {
    load helpers
    cd "$BATS_TEST_DIRNAME/../.." || return 1
}

make_fixture() {
    # $1: directory receiving the sink file; $2: file name; $3: sink body
    local d="${BATS_TEST_TMPDIR}/f"
    mkdir -p "$d/src" "$d/include" "$d/trans"
    printf '%s\n' "$3" > "$d/$1/$2"
    cat > "$d/trans/ru.yml" <<'YML'
translations:
  '%d lanes': '%s каналов'
YML
    printf '%s' "$d"
}

@test "a placeholder-swapped translation fails for a cpp-carried format" {
    local d
    d="$(make_fixture src a.cpp 'void f(int n) { char b[64]; snprintf(b, sizeof(b), lv_tr("%d lanes"), n); }')"
    run python3 "$GATE" --src "$d/src" --trans "$d/trans"
    [ "$status" -eq 1 ]
}

@test "a placeholder-swapped translation fails for a header-carried format" {
    local d
    d="$(make_fixture include a.h 'inline void f(int n) { char b[64]; snprintf(b, sizeof(b), lv_tr("%d lanes"), n); }')"
    run python3 "$GATE" --src "$d/include" --trans "$d/trans"
    [ "$status" -eq 1 ]
}

@test "a matching translation passes" {
    local d="${BATS_TEST_TMPDIR}/ok"
    mkdir -p "$d/src" "$d/trans"
    printf '%s\n' 'void f(int n) { char b[64]; snprintf(b, sizeof(b), lv_tr("%d lanes"), n); }' > "$d/src/a.cpp"
    cat > "$d/trans/ru.yml" <<'YML'
translations:
  '%d lanes': '%d каналов'
YML
    run python3 "$GATE" --src "$d/src" --trans "$d/trans"
    [ "$status" -eq 0 ]
}
