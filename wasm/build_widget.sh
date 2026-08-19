#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Phase 1: build the real filament_path_canvas widget to WASM.
set -euo pipefail
cd "$(dirname "$0")/.."   # worktree root
source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1

HARNESS="${1:-wasm/widget_main.cpp}"   # entry-point .cpp to link
OUT="${2:-wasm/out_widget/index.html}"
LOBJ=wasm/obj          # LVGL objects (shared with build.sh)
AOBJ=wasm/obj_app      # app / helix-xml / widget objects
mkdir -p "$LOBJ" "$AOBJ" "$(dirname "$OUT")"

DEF="-DHELIX_DISPLAY_SDL -DLV_CONF_INCLUDE_SIMPLE"
LVGL_INC="-Iwasm -isystem lib -isystem lib/lvgl -isystem lib/lvgl/src"
APP_INC="$LVGL_INC -Iinclude -Isrc -Ilib -Ilib/helix-xml/src -isystem lib/spdlog/include -isystem lib/libhv/include"

# ---- source groups ----
mapfile -t LVGL_SRCS < <(
  find lib/lvgl/src -name '*.c' ! -path '*/xml/*' ! -path '*/libs/expat/*' ! -path '*/drivers/*'
  find lib/lvgl/src/drivers/sdl -name '*.c'
)
mapfile -t HX_SRCS < <(find lib/helix-xml/src -name '*.c')
WIDGET_SRCS=(
  src/ui/ui_filament_path_canvas.cpp src/ui/ui_filament_path_topology.cpp
  src/ui/ui_filament_path_layers.cpp src/ui/ui_filament_path_glyphs.cpp
  src/ui/ui_filament_path_anim.cpp src/ui/filament_path_geometry.cpp
  src/ui/filament_tube_stroker.cpp
  src/rendering/nozzle_renderer_a4t.cpp src/rendering/nozzle_renderer_anthead.cpp
  src/rendering/nozzle_renderer_bambu.cpp src/rendering/nozzle_renderer_creality_k1.cpp
  src/rendering/nozzle_renderer_creality_k2.cpp src/rendering/nozzle_renderer_jabberwocky.cpp
  src/rendering/nozzle_renderer_stealthburner.cpp
  assets/fonts/noto_sans_12.c
  wasm/stubs_phase1.cpp wasm/helix_stubs.c
)

# ---- parallel compile helper: compile <src> <objdir> <include-flags> ----
export DEF
compile() {
  local src="$1" objdir="$2"; shift 2; local inc="$*"
  local obj="$objdir/$(echo "$src" | tr '/' '_').o"
  [[ -f "$obj" && "$obj" -nt "$src" ]] && return 0
  local std="-std=gnu11"; [[ "$src" == *.cpp ]] && std="-std=c++17"
  emcc -c -O1 $std $DEF $inc -sUSE_SDL=2 "$src" -o "$obj"
}
export -f compile

echo "LVGL: ${#LVGL_SRCS[@]}  helix-xml: ${#HX_SRCS[@]}  widget/app: ${#WIDGET_SRCS[@]}"
# LVGL + helix-xml are cached by source mtime (they don't change here). The
# widget/app TUs are always recompiled: this script does not track HEADER deps,
# and the widget TUs share ui_filament_path_internal.h — a stale object compiled
# against an old FilamentPathData layout is an ABI mismatch that crashes at run
# time. Recompiling ~17 files each build is cheap insurance.
for s in "${WIDGET_SRCS[@]}"; do rm -f "$AOBJ/$(echo "$s" | tr '/' '_').o"; done
printf '%s\n' "${LVGL_SRCS[@]}" | xargs -P "$(nproc)" -I{} bash -c 'compile "$1" "'"$LOBJ"'" '"$LVGL_INC"'' _ {}
printf '%s\n' "${HX_SRCS[@]}"   | xargs -P "$(nproc)" -I{} bash -c 'compile "$1" "'"$AOBJ"'" '"$APP_INC"'' _ {}
printf '%s\n' "${WIDGET_SRCS[@]}" | xargs -P "$(nproc)" -I{} bash -c 'compile "$1" "'"$AOBJ"'" '"$APP_INC"'' _ {}

echo "Linking $HARNESS -> $OUT ..."
em++ -O1 -std=c++17 $DEF $APP_INC -sUSE_SDL=2 \
  "$HARNESS" "$LOBJ"/*.o "$AOBJ"/*.o \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=134217728 -sEXIT_RUNTIME=0 -sSTACK_SIZE=2097152 \
  -o "$OUT"
echo "BUILD_OK -> $OUT"
