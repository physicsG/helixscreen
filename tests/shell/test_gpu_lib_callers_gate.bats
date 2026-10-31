#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Meta-tests for scripts/check_gpu_lib_callers.py: only reviewed objects may
# call into the runtime-loaded GPU libraries, or into the LVGL drivers that
# call them ungated. Each test builds a scratch build directory with real
# objects compiled by the host compiler, a trampoline table in the shape
# Implib.so writes, and its own allowlist.

GATE="scripts/check_gpu_lib_callers.py"

setup() {
    cd "$BATS_TEST_DIRNAME/../.." || return 1
    load helpers.bash
    BUILD="$BATS_TEST_TMPDIR/build"
    mkdir -p "$BUILD/implib"
    printf 'static const char *const sym_names[] = {\n  "drmModeGetResources",\n  "gbm_create_device",\n  0\n};\n' \
        > "$BUILD/implib/libdrm.so.2.init.c"
    ALLOWED="$BATS_TEST_TMPDIR/allowed.json"
    cat > "$ALLOWED" << 'EOF'
{"allowed": {"obj/api/backend.o": "gated", "obj/drivers/drm_driver.o": "driver"},
 "drivers": ["obj/drivers/drm_driver.o"]}
EOF
    # The reviewed backend calls the library and the driver; the driver calls
    # the library and exports drm_driver_create.
    object_calling obj/api/backend.o drmModeGetResources drm_driver_create
    object_defining obj/drivers/drm_driver.o drm_driver_create gbm_create_device
}

# Compile $BUILD/$1, a function that calls each remaining argument.
object_calling() {
    local obj="$BUILD/$1"
    shift
    mkdir -p "$(dirname "$obj")"
    {
        local fn
        for fn in "$@"; do printf 'void %s(void);\n' "$fn"; done
        printf 'void call_them(void) {'
        for fn in "$@"; do printf ' %s();' "$fn"; done
        printf ' }\n'
    } > "$BATS_TEST_TMPDIR/src.c"
    cc -c "$BATS_TEST_TMPDIR/src.c" -o "$obj"
}

# Compile $BUILD/$1, which defines function $2 and calls $3.
object_defining() {
    local obj="$BUILD/$1"
    mkdir -p "$(dirname "$obj")"
    printf 'void %s(void);\nvoid %s(void) { %s(); }\n' "$3" "$2" "$3" > "$BATS_TEST_TMPDIR/src.c"
    cc -c "$BATS_TEST_TMPDIR/src.c" -o "$obj"
}

gate() {
    run python3 "$GATE" --build-dir "$BUILD" --allowed "$ALLOWED"
}

@test "passes when only reviewed objects call a stubbed function or a driver" {
    object_calling obj/system/other.o printf
    gate
    [ "$status" -eq 0 ] || fail "expected a pass: $output"
    contains "2 reviewed objects" "$output"
}

@test "fails, naming the object and function, when an unreviewed object calls the library" {
    object_calling obj/ui/new_panel.o drmModeGetResources
    gate
    [ "$status" -eq 1 ] || fail "expected a failure: $output"
    contains "obj/ui/new_panel.o: drmModeGetResources" "$output"
}

@test "fails when an unreviewed object calls a driver's entry point" {
    object_calling obj/ui/new_panel.o drm_driver_create
    gate
    [ "$status" -eq 1 ] || fail "expected a failure: $output"
    contains "obj/ui/new_panel.o: drm_driver_create" "$output"
}

@test "ignores the trampoline objects themselves" {
    object_calling implib/libdrm.so.2.tramp.o drmModeGetResources
    gate
    [ "$status" -eq 0 ] || fail "expected a pass: $output"
}

@test "fails on an object with no symbol table" {
    object_calling obj/ui/stripped.o drmModeGetResources
    strip --strip-all "$BUILD/obj/ui/stripped.o"
    gate
    [ "$status" -eq 1 ] || fail "expected a failure: $output"
    contains "obj/ui/stripped.o: no symbol table" "$output"
}

@test "fails on an LTO object, whose calls nm cannot see" {
    mkdir -p "$BUILD/obj/ui"
    printf 'void drmModeGetResources(void);\nvoid f(void) { drmModeGetResources(); }\n' \
        > "$BATS_TEST_TMPDIR/lto.c"
    gcc -flto -c "$BATS_TEST_TMPDIR/lto.c" -o "$BUILD/obj/ui/lto.o"
    gate
    [ "$status" -eq 1 ] || fail "expected a failure: $output"
    contains "obj/ui/lto.o: an LTO object" "$output"
}

@test "fails when nm cannot read an object" {
    mkdir -p "$BUILD/obj/ui"
    echo "not an object" > "$BUILD/obj/ui/garbage.o"
    gate
    [ "$status" -eq 1 ] || fail "expected a failure: $output"
    contains "exited" "$output"
}

@test "fails on an allowlist entry that no longer calls in" {
    object_calling obj/api/backend.o printf
    gate
    [ "$status" -eq 1 ] || fail "a stale entry must not pass: $output"
    contains "obj/api/backend.o: listed in ALLOWED but calls into none" "$output"
}

@test "fails when there is no trampoline table to read" {
    rm "$BUILD/implib/libdrm.so.2.init.c"
    gate
    [ "$status" -eq 1 ] || fail "a gate with nothing to compare must not pass: $output"
    contains "no trampoline tables" "$output"
}
