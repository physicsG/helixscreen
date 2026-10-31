#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# gpu_libs_hidden_smoke.sh: run a real Pi/x86 helix-screen with its GPU
# libraries removed, and again with them installed.
#
#     scripts/ci/gpu_libs_hidden_smoke.sh run BIN
#
# BIN is a DRM build that carries libdrm, libgbm, libEGL, libGLESv2 and
# libinput as lazy-loading trampolines (mk/implib.mk). `run` needs root in a
# throwaway container that has those libraries installed, such as the x86
# toolchain image: it writes /dev/fb0 and moves the libraries away while it
# runs. With the libraries hidden, `--probe-egl` must exit 3 (libdrm missing);
# with them present and no DRM card, 1 (declined). Either way the app, started
# headless under --test with no backend forced, then drm, then egl, must come up
# on the framebuffer, call nothing in a missing library, and still be running
# when the timeout stops it. A release build has no mock printer, so --test
# connects to Moonraker: --moonraker points it at a port nothing listens on, so
# it can never reach a real printer whatever a config names. CI also runs the
# container with --network none.
#
# The fbdev backend gets a regular file at /dev/fb0 and scripts/ci/fake_fbdev.c,
# LD_PRELOADed, answers its ioctls. /dev/dri/card0 is a regular file too, so
# the DRM backend finds a card to inspect, as on a board that has one: with
# libdrm present its ioctls fail and the card is passed over, and with libdrm
# hidden nothing may call into the library to look at it.
#
# Subcommands, for tests/shell/test_gpu_libs_hidden_smoke.bats:
#     pass hidden|present BIN    the probe and the three app runs, no setup
#     check-run hidden|present FORCED RC LOG
#                                the verdict on one app run
#     hide DIR / restore DIR     move the libraries into DIR and back
#
# Env: GPU_SMOKE_TIMEOUT (seconds each app run must survive, default 20),
# GPU_SMOKE_LIB_DIRS (where hide looks), GPU_SMOKE_WORK (scratch directory).
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
timeout_s=${GPU_SMOKE_TIMEOUT:-20}
lib_dirs=${GPU_SMOKE_LIB_DIRS:-/usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /usr/lib /lib}
work=${GPU_SMOKE_WORK:-${TMPDIR:-/tmp}/gpu-libs-smoke}
lib_globs=(libdrm.so* libgbm.so* libEGL.so* libGLESv2.so* libinput.so*)
sonames=(libdrm.so.2 libgbm.so.1 libEGL.so.1 libGLESv2.so.2 libinput.so.10)

# What --probe-egl exits with (include/probe_egl_cmd.h).
readonly kProbeDeclined=1
readonly kProbeNoDrm=3
readonly kDeadMoonraker=ws://127.0.0.1:1

log() { printf '[gpu-smoke %s] %s\n' "$(date +%H:%M:%S)" "$*"; }
fail() { printf '[gpu-smoke %s] FAIL: %s\n' "$(date +%H:%M:%S)" "$*" >&2; }

usage() {
    sed -n '3,/^set -euo/{/^#/s/^# \{0,1\}//p}' "$0"
    exit 2
}

# check-run LIBS FORCED RC LOG: the verdict on one app run. RC is what
# `timeout` returned; 124 means the app was still running when it was stopped.
check_run() {
    local libs=$1 forced=$2 rc=$3 log_file=$4 ok=0 name
    name="libs $libs, backend ${forced:-auto}"
    if [ "$rc" -ne 124 ]; then
        fail "$name: exited with $rc before the ${timeout_s}s timeout"
        ok=1
    fi
    if [ -n "$forced" ] && ! grep -qF "HELIX_DISPLAY_BACKEND=$forced - using forced backend" "$log_file"; then
        fail "$name: the app never saw HELIX_DISPLAY_BACKEND=$forced"
        ok=1
    fi
    if ! grep -qF "Auto-detected: Framebuffer" "$log_file"; then
        fail "$name: did not come up on the framebuffer"
        ok=1
    fi
    # A call that reached a missing library past include/gpu_libs.h.
    if grep -E '\[GpuLibs\] (Called into .* which does not load|.* is unavailable; the call fails)' "$log_file"; then
        fail "$name: called into a library that does not load"
        ok=1
    fi
    case "$libs" in
    hidden)
        if ! grep -qF "[GpuLibs] libdrm.so.2 does not load" "$log_file"; then
            fail "$name: libdrm.so.2 still loads, so nothing was hidden"
            ok=1
        fi
        ;;
    present)
        if grep -E '\[GpuLibs\] lib[A-Za-z0-9]+\.so\.[0-9]+ does not load' "$log_file"; then
            fail "$name: a library that should be installed does not load"
            ok=1
        fi
        ;;
    *)
        fail "check-run: libs must be hidden or present, not '$libs'"
        return 2
        ;;
    esac
    [ "$ok" -eq 0 ] && log "ok: $name"
    return "$ok"
}

# pass LIBS BIN: the probe, then the app with no backend forced, drm and egl.
run_pass() {
    local libs=$1 bin=$2 want rc forced log_file failed=0
    case "$libs" in
    hidden) want=$kProbeNoDrm ;;
    present) want=$kProbeDeclined ;;
    *) fail "pass: libs must be hidden or present, not '$libs'"; return 2 ;;
    esac
    mkdir -p "$work"

    rc=0
    "$bin" --probe-egl > "$work/probe-$libs.log" 2>&1 || rc=$?
    log "probe ($libs): exit $rc: $(head -n 1 "$work/probe-$libs.log")"
    if [ "$rc" -ne "$want" ]; then
        fail "libs $libs: --probe-egl exited $rc, expected $want"
        failed=1
    fi

    for forced in "" drm egl; do
        log_file="$work/app-$libs-${forced:-auto}.log"
        local config_dir="$work/config-$libs-${forced:-auto}"
        rm -rf "$config_dir"
        mkdir -p "$config_dir"
        local backend=(-u HELIX_DISPLAY_BACKEND)
        [ -z "$forced" ] || backend=("HELIX_DISPLAY_BACKEND=$forced")
        rc=0
        env "${backend[@]}" HELIX_CONFIG_DIR="$config_dir" \
            timeout -s TERM -k 5 "$timeout_s" "$bin" --test -vv \
            --moonraker "$kDeadMoonraker" > "$log_file" 2>&1 || rc=$?
        if ! check_run "$libs" "$forced" "$rc" "$log_file"; then
            failed=1
            echo "---- last lines of $log_file ----" >&2
            tail -n 40 "$log_file" >&2
            echo "----" >&2
        fi
    done
    return "$failed"
}

# hide DIR: move every copy of the GPU libraries out of the library path into DIR.
hide_libs() {
    local stash=$1 dir real glob f moved=0
    mkdir -p "$stash"
    local -A seen=()
    for dir in $lib_dirs; do
        [ -d "$dir" ] || continue
        real=$(cd "$dir" && pwd -P)
        [ -z "${seen[$real]:-}" ] || continue
        seen[$real]=1
        for glob in "${lib_globs[@]}"; do
            for f in "$real"/$glob; do
                [ -e "$f" ] || [ -L "$f" ] || continue
                mkdir -p "$stash$real"
                mv "$f" "$stash$real/"
                moved=$((moved + 1))
            done
        done
    done
    if [ "$moved" -eq 0 ]; then
        fail "hide: no GPU library found under $lib_dirs"
        return 1
    fi
    if command -v ldconfig > /dev/null 2>&1 && [ "$(id -u)" -eq 0 ]; then
        ldconfig
    fi
    log "hid $moved files"
}

# restore DIR: put back what hide moved into DIR.
restore_libs() {
    local stash=$1 f dest
    [ -d "$stash" ] || return 0
    while IFS= read -r -d '' f; do
        dest=${f#"$stash"}
        mv "$f" "$dest"
    done < <(find "$stash" \( -type f -o -type l \) -print0)
    rm -rf "$stash"
    if command -v ldconfig > /dev/null 2>&1 && [ "$(id -u)" -eq 0 ]; then
        ldconfig
    fi
}

run_all() {
    local bin=$1 failed=0 soname
    if [ "$(id -u)" -ne 0 ]; then
        fail "run needs root in a throwaway container: it writes /dev/fb0 and hides system libraries"
        return 2
    fi
    [ -x "$bin" ] || { fail "$bin is not an executable"; return 2; }
    mkdir -p "$work"

    for soname in "${sonames[@]}"; do
        if ! ldconfig -p | grep -qF "$soname"; then
            fail "$soname is not installed here, so the present pass cannot run"
            return 2
        fi
    done

    cc -shared -fPIC -o "$work/fake_fbdev.so" "$here/fake_fbdev.c" -ldl
    rm -f /dev/fb0
    truncate -s $((800 * 480 * 4)) /dev/fb0
    mkdir -p /dev/dri
    rm -f /dev/dri/card0
    : > /dev/dri/card0
    export LD_PRELOAD="$work/fake_fbdev.so"
    export FAKE_FBDEV=/dev/fb0

    log "pass 1: GPU libraries present"
    run_pass present "$bin" || failed=1
    if ! grep -qF "Checking DRM device: /dev/dri/card0" "$work/app-present-auto.log"; then
        fail "libs present: the DRM backend never inspected /dev/dri/card0"
        failed=1
    fi

    log "pass 2: GPU libraries hidden"
    trap 'restore_libs "$work/hidden"' EXIT
    hide_libs "$work/hidden"
    for soname in "${sonames[@]}"; do
        if ldconfig -p | grep -qF "$soname"; then
            fail "$soname is still in the loader cache after hiding"
            failed=1
        fi
    done
    run_pass hidden "$bin" || failed=1
    restore_libs "$work/hidden"
    trap - EXIT

    if [ "$failed" -ne 0 ]; then
        fail "GPU-libraries-hidden smoke failed (logs in $work)"
        return 1
    fi
    log "PASS: both passes"
}

[ $# -ge 1 ] || usage
cmd=$1
shift
case "$cmd" in
run) [ $# -eq 1 ] || usage; run_all "$1" ;;
pass) [ $# -eq 2 ] || usage; run_pass "$1" "$2" ;;
check-run) [ $# -eq 4 ] || usage; check_run "$@" ;;
hide) [ $# -eq 1 ] || usage; hide_libs "$1" ;;
restore) [ $# -eq 1 ] || usage; restore_libs "$1" ;;
*) usage ;;
esac
