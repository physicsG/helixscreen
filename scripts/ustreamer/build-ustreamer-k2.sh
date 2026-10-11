#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# build-ustreamer-k2.sh — Cross-build a fully-static armv7 ustreamer (MJPEG
# camera server) for the Creality K2 target.
#
# This script is meant to run INSIDE the `helixscreen/toolchain-k2` docker
# image (musl Bootlin toolchain, gcc 12.3.0). It builds three static
# dependencies into a private prefix and links a static ustreamer binary:
#
#   1. libjpeg-turbo 3.0.4  (cmake, NEON SIMD via C intrinsics — no nasm)
#   2. libevent 2.1.12      (autotools, no openssl/mbedtls/tests/benchmarks)
#   3. ustreamer 6.39       (pikvm/ustreamer, LDFLAGS=-static)
#
# Output: $OUT_DIR/ustreamer  (default: build/k2/bin/ustreamer)
# Source tarballs are cached under $CACHE_DIR (default: build/.cache/ustreamer)
# so repeat runs / CI don't re-download.
#
# Idempotent: if a valid static ARM ustreamer already exists at the output
# path, the script verifies it and exits 0 without rebuilding. Pass
# FORCE_REBUILD=1 to rebuild unconditionally.
#
# Invoked by the `ustreamer-k2` make target (see mk/cross.mk), which wraps
# this in the docker container under scripts/pool-docker.sh.

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration (pinned versions + checksums)
# ---------------------------------------------------------------------------
LIBJPEG_TURBO_VERSION="3.0.4"
LIBJPEG_TURBO_SHA256="99130559e7d62e8d695f2c0eaeef912c5828d5b84a0537dcb24c9678c9d5b76b"
LIBJPEG_TURBO_URL="https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/${LIBJPEG_TURBO_VERSION}/libjpeg-turbo-${LIBJPEG_TURBO_VERSION}.tar.gz"

LIBEVENT_VERSION="2.1.12"
LIBEVENT_SHA256="92e6de1be9ec176428fd2367677e61ceffc2ee1cb119035037a27d346b0403bb"
LIBEVENT_URL="https://github.com/libevent/libevent/releases/download/release-${LIBEVENT_VERSION}-stable/libevent-${LIBEVENT_VERSION}-stable.tar.gz"

USTREAMER_VERSION="6.39"
USTREAMER_SHA256="72ec8532b4138ff70c681464fd9271cd3c1c01636d17268df837ce136ed40313"
USTREAMER_URL="https://github.com/pikvm/ustreamer/archive/refs/tags/v${USTREAMER_VERSION}.tar.gz"

# Cross toolchain (matches docker/Dockerfile.k2)
CROSS_PREFIX="${CROSS_PREFIX:-arm-buildroot-linux-musleabihf-}"
export CC="${CROSS_PREFIX}gcc"
export AR="${CROSS_PREFIX}ar"
export RANLIB="${CROSS_PREFIX}ranlib"
export STRIP="${CROSS_PREFIX}strip"

# armv7-a + NEON hard-float, mirrors mk/cross.mk K2 TARGET_CFLAGS (release subset)
ARCH_CFLAGS="-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -O2"

# Paths (resolve relative to repo root, which is the docker workdir /src)
REPO_ROOT="${REPO_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
OUT_DIR="${OUT_DIR:-${REPO_ROOT}/build/k2/bin}"
CACHE_DIR="${CACHE_DIR:-${REPO_ROOT}/build/.cache/ustreamer}"
WORK_DIR="${WORK_DIR:-${REPO_ROOT}/build/.work/ustreamer-k2}"
DEPS_PREFIX="${WORK_DIR}/deps"   # static libjpeg-turbo + libevent install here

OUT_BIN="${OUT_DIR}/ustreamer"

# JOBPOOL_SLOTS is the share pool-docker.sh holds for this container.
NPROC="${JOBPOOL_SLOTS:-$(nproc 2>/dev/null || echo 4)}"

log()  { printf '\033[36m[ustreamer-k2]\033[0m %s\n' "$*"; }
warn() { printf '\033[33m[ustreamer-k2]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[31m[ustreamer-k2] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

# is_valid_static_arm <path> : returns 0 if file is a static 32-bit ARM ELF exe
is_valid_static_arm() {
    local f="$1"
    [ -f "$f" ] || return 1
    local desc
    desc="$(file -b "$f" 2>/dev/null || true)"
    case "$desc" in
        *"ELF 32-bit LSB"*"ARM"*"statically linked"*) return 0 ;;
        *) return 1 ;;
    esac
}

# fetch <url> <sha256> <dest> : download (cached) and verify checksum
fetch() {
    local url="$1" sha="$2" dest="$3"
    mkdir -p "$(dirname "$dest")"
    if [ -f "$dest" ] && echo "${sha}  ${dest}" | sha256sum -c --status 2>/dev/null; then
        log "cached: $(basename "$dest")"
        return 0
    fi
    log "downloading: $url"
    wget -q -O "${dest}.tmp" "$url" || die "download failed: $url"
    echo "${sha}  ${dest}.tmp" | sha256sum -c --status \
        || die "checksum mismatch for $(basename "$dest") (expected $sha)"
    mv -f "${dest}.tmp" "$dest"
    log "verified: $(basename "$dest")"
}

# ---------------------------------------------------------------------------
# Idempotency: skip if a valid binary already exists
# ---------------------------------------------------------------------------
if [ "${FORCE_REBUILD:-0}" != "1" ] && is_valid_static_arm "$OUT_BIN"; then
    log "up to date — valid static ARM binary already at ${OUT_BIN}"
    file -b "$OUT_BIN"
    exit 0
fi

command -v "$CC" >/dev/null 2>&1 || die "cross compiler '$CC' not found — run inside helixscreen/toolchain-k2"

log "cross compiler: $($CC --version | head -1)"
log "output:         ${OUT_BIN}"
log "cache:          ${CACHE_DIR}"

mkdir -p "$OUT_DIR" "$CACHE_DIR" "$DEPS_PREFIX"
rm -rf "${WORK_DIR}/src"
mkdir -p "${WORK_DIR}/src"

# ---------------------------------------------------------------------------
# Fetch sources
# ---------------------------------------------------------------------------
LIBJPEG_TAR="${CACHE_DIR}/libjpeg-turbo-${LIBJPEG_TURBO_VERSION}.tar.gz"
LIBEVENT_TAR="${CACHE_DIR}/libevent-${LIBEVENT_VERSION}-stable.tar.gz"
USTREAMER_TAR="${CACHE_DIR}/ustreamer-${USTREAMER_VERSION}.tar.gz"

fetch "$LIBJPEG_TURBO_URL" "$LIBJPEG_TURBO_SHA256" "$LIBJPEG_TAR"
fetch "$LIBEVENT_URL"      "$LIBEVENT_SHA256"      "$LIBEVENT_TAR"
fetch "$USTREAMER_URL"     "$USTREAMER_SHA256"     "$USTREAMER_TAR"

# ---------------------------------------------------------------------------
# 1. libjpeg-turbo (static, NEON SIMD on — C intrinsics, no nasm needed)
# ---------------------------------------------------------------------------
log "building libjpeg-turbo ${LIBJPEG_TURBO_VERSION} (static)"
tar -xzf "$LIBJPEG_TAR" -C "${WORK_DIR}/src"
JPEG_SRC="${WORK_DIR}/src/libjpeg-turbo-${LIBJPEG_TURBO_VERSION}"
JPEG_BUILD="${JPEG_SRC}/build"
mkdir -p "$JPEG_BUILD"
cat > "${WORK_DIR}/k2-toolchain.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER ${CC})
set(CMAKE_AR ${AR})
set(CMAKE_RANLIB ${RANLIB})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF
cmake -S "$JPEG_SRC" -B "$JPEG_BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="${WORK_DIR}/k2-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$DEPS_PREFIX" \
    -DCMAKE_C_FLAGS="$ARCH_CFLAGS" \
    -DENABLE_SHARED=0 \
    -DENABLE_STATIC=1 \
    -DWITH_TURBOJPEG=1 \
    >/dev/null
cmake --build "$JPEG_BUILD" -j"$NPROC" >/dev/null
cmake --install "$JPEG_BUILD" >/dev/null
log "  installed libjpeg-turbo into ${DEPS_PREFIX}"

# libjpeg-turbo may install into lib or lib64 depending on host; normalize.
if [ -d "${DEPS_PREFIX}/lib64" ] && [ ! -e "${DEPS_PREFIX}/lib/libjpeg.a" ]; then
    mkdir -p "${DEPS_PREFIX}/lib"
    cp -a "${DEPS_PREFIX}/lib64/." "${DEPS_PREFIX}/lib/"
fi

# ---------------------------------------------------------------------------
# 2. libevent (static, no openssl/mbedtls/tests/benchmarks/samples)
# ---------------------------------------------------------------------------
log "building libevent ${LIBEVENT_VERSION} (static)"
tar -xzf "$LIBEVENT_TAR" -C "${WORK_DIR}/src"
EVENT_SRC="${WORK_DIR}/src/libevent-${LIBEVENT_VERSION}-stable"
(
    cd "$EVENT_SRC"
    CFLAGS="$ARCH_CFLAGS" ./configure \
        --host=arm-buildroot-linux-musleabihf \
        --prefix="$DEPS_PREFIX" \
        --enable-static \
        --disable-shared \
        --disable-openssl \
        --disable-mbedtls \
        --disable-debug-mode \
        --disable-samples \
        --disable-libevent-regress \
        >/dev/null
    make -j"$NPROC" >/dev/null
    make install >/dev/null
)
log "  installed libevent into ${DEPS_PREFIX}"

# ---------------------------------------------------------------------------
# 3. ustreamer (fully static)
# ---------------------------------------------------------------------------
log "building ustreamer ${USTREAMER_VERSION} (static)"
tar -xzf "$USTREAMER_TAR" -C "${WORK_DIR}/src"
US_SRC="${WORK_DIR}/src/ustreamer-${USTREAMER_VERSION}"
(
    cd "$US_SRC"
    export CC="$CC"
    export CFLAGS="$ARCH_CFLAGS -I${DEPS_PREFIX}/include"
    # -static for a fully static link; pull in libatomic (present in sysroot).
    export LDFLAGS="-static -L${DEPS_PREFIX}/lib -latomic"
    # ustreamer's makefile looks up libjpeg/libevent via pkg-config when
    # available; point it at our private prefix so it finds the static .pc files.
    export PKG_CONFIG_PATH="${DEPS_PREFIX}/lib/pkgconfig:${DEPS_PREFIX}/lib64/pkgconfig"
    export PKG_CONFIG_LIBDIR="${DEPS_PREFIX}/lib/pkgconfig:${DEPS_PREFIX}/lib64/pkgconfig"

    make \
        WITH_PYTHON=0 \
        WITH_SETPROCTITLE=0 \
        WITH_JANUS=0 \
        WITH_V4P=0 \
        WITH_GPIO=0 \
        WITH_SYSTEMD=0 \
        WITH_PTHREAD_NP=1 \
        WITH_PDEATHSIG=1 \
        -j"$NPROC"
)

[ -f "${US_SRC}/ustreamer" ] || die "ustreamer build produced no binary"
"$STRIP" "${US_SRC}/ustreamer"
cp -f "${US_SRC}/ustreamer" "$OUT_BIN"

# ---------------------------------------------------------------------------
# Verify
# ---------------------------------------------------------------------------
log "verifying ${OUT_BIN}"
file -b "$OUT_BIN"
is_valid_static_arm "$OUT_BIN" || die "produced binary is not a static ARM ELF"
if "${CROSS_PREFIX}readelf" -d "$OUT_BIN" 2>/dev/null | grep -q 'NEEDED'; then
    die "binary has a dynamic section (NEEDED entries) — not fully static"
fi
log "OK: fully-static armv7 ustreamer at ${OUT_BIN} ($(du -h "$OUT_BIN" | cut -f1))"
