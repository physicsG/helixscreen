#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build a HelixScreen LVGL harness to WebAssembly.
#   Usage: wasm/build.sh [harness.cpp] [out/index.html]
# Compiles LVGL core + the SDL driver (other platform drivers excluded — they
# pull Linux-only headers Emscripten lacks) to cached objects in parallel, then
# links the given harness. Re-runs only recompile changed sources.
set -euo pipefail
cd "$(dirname "$0")/.."   # worktree root
source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1

HARNESS="${1:-wasm/smoke_main.cpp}"
OUT="${2:-wasm/out/index.html}"
OBJDIR="wasm/obj"
mkdir -p "$OBJDIR" "$(dirname "$OUT")"

DEFINES="-DHELIX_DISPLAY_SDL -DLV_CONF_INCLUDE_SIMPLE"
INCLUDES="-Iwasm -isystem lib -isystem lib/lvgl"
export CFLAGS="-O1 $DEFINES $INCLUDES -sUSE_SDL=2"
export OBJDIR

# 1. LVGL sources: everything except the XML engine (helix-xml owns it), bundled
#    expat, and all platform drivers except SDL.
mapfile -t SRCS < <(
  find lib/lvgl/src -name '*.c' \
    ! -path '*/xml/*' ! -path '*/libs/expat/*' ! -path '*/drivers/*'
  find lib/lvgl/src/drivers/sdl -name '*.c'
)
echo "Compiling ${#SRCS[@]} LVGL sources (parallel)..."

compile_one() {
  local src="$1"
  local obj="$OBJDIR/$(echo "$src" | tr '/' '_').o"
  [[ -f "$obj" && "$obj" -nt "$src" ]] && return 0
  emcc -c $CFLAGS "$src" -o "$obj"
}
export -f compile_one
printf '%s\n' "${SRCS[@]}" | xargs -P "$(nproc)" -I{} bash -c 'compile_one "$@"' _ {}

# 2. Link the harness against the LVGL objects.
echo "Linking $HARNESS -> $OUT ..."
em++ -O1 -std=c++17 $DEFINES $INCLUDES -sUSE_SDL=2 \
  "$HARNESS" wasm/helix_stubs.c "$OBJDIR"/*.o \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 -sEXIT_RUNTIME=0 \
  -sSTACK_SIZE=1048576 \
  -o "$OUT"
echo "BUILD_OK -> $OUT"
