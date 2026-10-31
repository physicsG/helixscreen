#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Verdict logic of scripts/ci/gpu_libs_hidden_smoke.sh, driven by a fake
# helix-screen that answers --probe-egl with FAKE_PROBE_RC and otherwise prints
# FAKE_LOG and runs (or exits with FAKE_EXIT). The real binary runs in CI.

SCRIPT="scripts/ci/gpu_libs_hidden_smoke.sh"

setup() {
    cd "$BATS_TEST_DIRNAME/../.." || return 1
    load helpers.bash
    export GPU_SMOKE_WORK="$BATS_TEST_TMPDIR/work"
    export GPU_SMOKE_TIMEOUT=1
    FAKE="$BATS_TEST_TMPDIR/helix-screen"
    cat > "$FAKE" << 'EOF'
#!/bin/sh
if [ "$1" = --probe-egl ]; then
    echo "probe answer"
    exit "${FAKE_PROBE_RC:-0}"
fi
if [ -n "${HELIX_DISPLAY_BACKEND:-}" ]; then
    echo "[DisplayBackend] HELIX_DISPLAY_BACKEND=$HELIX_DISPLAY_BACKEND - using forced backend"
fi
printf '%b\n' "$FAKE_LOG"
[ -z "${FAKE_EXIT:-}" ] || exit "$FAKE_EXIT"
exec sleep 30
EOF
    chmod +x "$FAKE"
    HIDDEN_LOG='[GpuLibs] libdrm.so.2 does not load: not found\n[DisplayBackend] Auto-detected: Framebuffer'
    PRESENT_LOG='[DisplayBackend] Auto-detected: Framebuffer'
}

@test "hidden: probe exits 3 and every run stays up on the framebuffer" {
    FAKE_PROBE_RC=3 FAKE_LOG="$HIDDEN_LOG" run "$SCRIPT" pass hidden "$FAKE"
    [ "$status" -eq 0 ]
    contains "ok: libs hidden, backend auto" "$output"
    contains "ok: libs hidden, backend drm" "$output"
    contains "ok: libs hidden, backend egl" "$output"
}

@test "present: probe exits 1 and every run stays up on the framebuffer" {
    FAKE_PROBE_RC=1 FAKE_LOG="$PRESENT_LOG" run "$SCRIPT" pass present "$FAKE"
    [ "$status" -eq 0 ]
    contains "ok: libs present, backend egl" "$output"
}

@test "hidden: a probe that declines instead of reporting no libdrm fails" {
    FAKE_PROBE_RC=1 FAKE_LOG="$HIDDEN_LOG" run "$SCRIPT" pass hidden "$FAKE"
    [ "$status" -ne 0 ]
    contains "--probe-egl exited 1, expected 3" "$output"
}

@test "present: a probe that reports no libdrm fails" {
    FAKE_PROBE_RC=3 FAKE_LOG="$PRESENT_LOG" run "$SCRIPT" pass present "$FAKE"
    [ "$status" -ne 0 ]
    contains "--probe-egl exited 3, expected 1" "$output"
}

@test "an app that exits before the timeout fails" {
    FAKE_PROBE_RC=3 FAKE_LOG="$HIDDEN_LOG" FAKE_EXIT=0 run "$SCRIPT" pass hidden "$FAKE"
    [ "$status" -ne 0 ]
    contains "exited with 0 before the 1s timeout" "$output"
}

@test "an app that crashes fails" {
    FAKE_PROBE_RC=3 FAKE_LOG="$HIDDEN_LOG" FAKE_EXIT=139 run "$SCRIPT" pass hidden "$FAKE"
    [ "$status" -ne 0 ]
    contains "exited with 139" "$output"
}

@test "an app that never reaches the framebuffer fails" {
    FAKE_PROBE_RC=3 FAKE_LOG='[GpuLibs] libdrm.so.2 does not load: not found' \
        run "$SCRIPT" pass hidden "$FAKE"
    [ "$status" -ne 0 ]
    contains "did not come up on the framebuffer" "$output"
}

@test "a call into a missing library fails the run" {
    FAKE_PROBE_RC=3 FAKE_LOG="$HIDDEN_LOG"'\n[GpuLibs] drmModeGetResources is unavailable; the call fails' \
        run "$SCRIPT" pass hidden "$FAKE"
    [ "$status" -ne 0 ]
    contains "called into a library that does not load" "$output"
}

@test "a missing library's loader being reached fails the run" {
    FAKE_PROBE_RC=3 FAKE_LOG="$HIDDEN_LOG"'\n[GpuLibs] Called into libdrm.so.2 which does not load; its functions fail' \
        run "$SCRIPT" pass hidden "$FAKE"
    [ "$status" -ne 0 ]
    contains "called into a library that does not load" "$output"
}

@test "hidden: libdrm still loading means nothing was hidden" {
    FAKE_PROBE_RC=3 FAKE_LOG="$PRESENT_LOG" run "$SCRIPT" pass hidden "$FAKE"
    [ "$status" -ne 0 ]
    contains "libdrm.so.2 still loads, so nothing was hidden" "$output"
}

@test "present: a library that does not load fails" {
    FAKE_PROBE_RC=1 FAKE_LOG='[GpuLibs] libinput.so.10 does not load: not found\n'"$PRESENT_LOG" \
        run "$SCRIPT" pass present "$FAKE"
    [ "$status" -ne 0 ]
    contains "a library that should be installed does not load" "$output"
}

@test "a forced backend the app never saw fails" {
    log="$BATS_TEST_TMPDIR/app.log"
    printf '%b\n' "$HIDDEN_LOG" > "$log"
    run "$SCRIPT" check-run hidden drm 124 "$log"
    [ "$status" -ne 0 ]
    contains "never saw HELIX_DISPLAY_BACKEND=drm" "$output"
}

@test "run refuses outside a root container" {
    [ "$(id -u)" -ne 0 ] || skip "running as root"
    run "$SCRIPT" run "$FAKE"
    [ "$status" -eq 2 ]
    contains "needs root" "$output"
}

@test "hide moves every GPU library away and restore puts them back" {
    libdir="$BATS_TEST_TMPDIR/lib"
    mkdir -p "$libdir"
    for f in libdrm.so.2.4.0 libgbm.so.1.0.0 libEGL.so.1.1.0 libGLESv2.so.2.1.0 \
        libinput.so.10.13.0 libc.so.6; do
        : > "$libdir/$f"
    done
    ln -s libdrm.so.2.4.0 "$libdir/libdrm.so.2"
    stash="$BATS_TEST_TMPDIR/stash"

    GPU_SMOKE_LIB_DIRS="$libdir" run "$SCRIPT" hide "$stash"
    [ "$status" -eq 0 ]
    contains "hid 6 files" "$output"
    [ "$(ls "$libdir")" = libc.so.6 ]

    GPU_SMOKE_LIB_DIRS="$libdir" run "$SCRIPT" restore "$stash"
    [ "$status" -eq 0 ]
    [ "$(ls "$libdir" | wc -l)" -eq 7 ]
    [ -L "$libdir/libdrm.so.2" ]
    [ ! -e "$stash" ]
}

@test "hide fails when there is nothing to hide" {
    libdir="$BATS_TEST_TMPDIR/empty"
    mkdir -p "$libdir"
    GPU_SMOKE_LIB_DIRS="$libdir" run "$SCRIPT" hide "$BATS_TEST_TMPDIR/stash"
    [ "$status" -ne 0 ]
    contains "no GPU library found" "$output"
}

# The release job's `if`, evaluated for every result the smoke can end with. A
# timed-out smoke ends 'cancelled', and that must hold the release as a
# failure does. An unknown term in the expression is a NameError, so a rewrite
# fails here instead of passing unexamined.
@test "release publishes only after the smoke succeeded or was skipped" {
    run python3 - .github/workflows/release.yml <<'PY'
import json, re, sys, yaml
with open(sys.argv[1]) as fh:
    expr = yaml.safe_load(fh)["jobs"]["release"]["if"]
py = re.fullmatch(r"\s*\$\{\{(.*)\}\}\s*", expr, re.S).group(1)
py = re.sub(r"contains\(fromJSON\('([^']*)'\),\s*([^)]*)\)", r"(\2 in json.loads('\1'))", py)
py = re.sub(r"needs\.([\w-]+)\.result", lambda m: f"R[{m.group(1)!r}]", py)
py = (py.replace("!cancelled()", "True").replace("!=", " NE ")
        .replace("&&", " and ").replace("||", " or ").replace("!", " not ")
        .replace(" NE ", " != "))
want = {"success": True, "skipped": True, "failure": False, "cancelled": False}
bad = []
for smoke, publish in want.items():
    R = {"plan": "success", "build-platforms": "success",
         "build-android": "success", "gpu-libs-hidden-smoke": smoke}
    got = eval(py, {"json": json}, {"R": R})
    if got != publish:
        bad.append(f"smoke {smoke}: release runs={got}, want {publish}")
print("\n".join(bad))
sys.exit(1 if bad else 0)
PY
    [ "$status" -eq 0 ] || fail "$output"
}
