#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build the REAL HelixScreen AMS pages to WebAssembly.
#
# Unlike build_widget.sh (which links a handful of widget TUs against a stub
# layer), this compiles the app source manifest in wasm/app_srcs.txt -- the
# production panels, XML engine, theme manager, subject graph and AMS backends
# -- and boots them from wasm/app_main.cpp. See wasm/README.md.
set -euo pipefail
cd "$(dirname "$0")/.."
command -v emcc >/dev/null 2>&1 || source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1

OUT="${1:-wasm/out_app/index.html}"
LOBJ=wasm/obj_app_lvgl    # LVGL + helix-xml (cached by mtime; never change here)
AOBJ=wasm/obj_app         # app TUs + fonts + boot + stubs
mkdir -p "$LOBJ" "$AOBJ" "$(dirname "$OUT")"

# ---- flags -------------------------------------------------------------------
# HELIX_WASM_APP switches wasm/lv_conf.h onto the real Noto faces + the real
# assert handler. The PCH force-include is NOT optional: the native build feeds
# include/lvgl_pch.h to every TU, and it is the only place most files get their
# lv_xml_* declarations and hv/json.hpp.
VERSION="$(sed -n 's/^VERSION *:= *//p' Makefile | head -1)"; VERSION="${VERSION:-0.0.0-wasm}"
DEF="-DHELIX_WASM_APP -DHELIX_DISPLAY_SDL -DLV_CONF_INCLUDE_SIMPLE"
DEF="$DEF -DHELIX_VERSION=\"$VERSION\" -DHELIX_VERSION_MAJOR=0 -DHELIX_VERSION_MINOR=0 -DHELIX_VERSION_PATCH=0"
DEF="$DEF -DINSTALLER_FILENAME=\"install.sh\" -DHELIX_MAX_FONT_TIER=6"
DEF="$DEF -DHELIX_ENABLE_MOCKS -DHELIX_ENABLE_REMOTE_CONTROL"
DEF="$DEF -DHELIX_HAS_LABEL_PRINTER=0 -DHELIX_HAS_CFS=1 -DHELIX_HAS_IFS=1"
DEF="$DEF -DHELIX_HAS_GCODE_VIEWER=0 -DHELIX_HAS_BED_MESH_3D=0 -DHELIX_HAS_PLUGINS=0"
DEF="$DEF -DHELIX_HAS_TIMELAPSE_VIEWER=0"

LVGL_INC="-Iwasm -I. -Iinclude -isystem lib -isystem lib/lvgl -isystem lib/lvgl/src"
APP_INC="$LVGL_INC -Iinclude -Isrc -Isrc/generated -Ibuild/generated"
APP_INC="$APP_INC -isystem lib/glm -isystem lib/spdlog/include"
APP_INC="$APP_INC -isystem lib/libhv/include -isystem lib/libhv/cpputil -isystem lib/libhv"
APP_INC="$APP_INC -isystem lib/stb -isystem lib/lv_markdown/src -isystem lib/lv_markdown/deps/md4c -isystem lib/quirc/lib"

# ---- source groups -----------------------------------------------------------
mapfile -t LVGL_SRCS < <(
  find lib/lvgl/src -name '*.c' ! -path '*/xml/*' ! -path '*/libs/expat/*' ! -path '*/drivers/*'
  find lib/lvgl/src/drivers/sdl -name '*.c'
)
mapfile -t HX_SRCS  < <(find lib/helix-xml/src -name '*.c')
# quirc: the QR decoder behind the "add printer by QR" flow. Small, pure C,
# WASM-clean — cheaper to compile than to stub its nine entry points.
mapfile -t QUIRC_SRCS < <(find lib/quirc/lib -name '*.c')
# lv_markdown + md4c: <ui_markdown> is registered by register_xml_components(),
# so the whole XML registry needs it even though no AMS screen renders markdown.
MD_SRCS=(lib/lv_markdown/src/lv_markdown.c lib/lv_markdown/src/lv_markdown_style.c
         lib/lv_markdown/deps/md4c/md4c.c)
mapfile -t APP_SRCS < <(grep -E '^[^#].*\.(cpp|c)$' wasm/app_srcs.txt)
mapfile -t FONT_SRCS < <(ls assets/fonts/*.c)
# helix_stubs.c is for the widget-only harnesses: it no-ops the helix_* hooks the
# patches bake into LVGL. The app build defines them for real (application.cpp),
# so including it here is a duplicate-symbol link error.
HARNESS_SRCS=(wasm/app_main.cpp wasm/scripted_u1.cpp wasm/stubs_app.cpp)

# ---- compile -----------------------------------------------------------------
export DEF
# Rebuild decisions are made against the DEPFILE, not just the .cpp's mtime.
# Header-blind caching is not a slow build, it is a crashing one: adding a
# virtual to ams_backend.h shifts every later vtable slot, and a TU that kept its
# stale object then calls through the wrong one. That surfaces in the browser as
# a bare "function signature mismatch" with no file named, minutes after a build
# that reported success.
compile() {
  local src="$1" objdir="$2"; shift 2; local inc="$*"
  local obj="$objdir/$(echo "$src" | tr '/' '_').o"
  local dep="${obj%.o}.d"
  if [[ -f "$obj" && -f "$dep" ]]; then
    # Every prerequisite emcc recorded must be older than the object. Objects
    # from a build before depfiles existed have no .d and always recompile once.
    local fresh=1 f
    for f in $(sed -e 's/^[^:]*://' -e 's/\\$//' "$dep"); do
      [[ -e "$f" && "$f" -nt "$obj" ]] && { fresh=0; break; }
    done
    [[ $fresh -eq 1 ]] && return 0
  fi
  # The PCH is C++-only: it pulls <algorithm>, spdlog and nlohmann, none of which
  # a .c TU (helix-xml, the font tables) can parse.
  local std="-std=gnu11" pch=""
  if [[ "$src" == *.cpp ]]; then std="-std=c++17"; pch="-include include/lvgl_pch.h"; fi
  # -fexceptions is mandatory, not a tuning knob: Emscripten compiles try/catch
  # away by default, so a `throw` traps as `unreachable`. nlohmann::json throws
  # on every type mismatch and the JSON-RPC dispatcher reports errors that way.
  emcc -c -O1 $std $pch $DEF $inc -fexceptions -sUSE_SDL=2 -sUSE_ZLIB=1 \
       -MMD -MF "$dep" "$src" -o "$obj"
}
export -f compile

echo "LVGL: ${#LVGL_SRCS[@]}  helix-xml: ${#HX_SRCS[@]}  quirc: ${#QUIRC_SRCS[@]}  app: ${#APP_SRCS[@]}  fonts: ${#FONT_SRCS[@]}"
J="$(nproc)"
printf '%s\n' "${LVGL_SRCS[@]}"    | xargs -P "$J" -I{} bash -c 'compile "$1" "'"$LOBJ"'" '"$LVGL_INC"''  _ {}
printf '%s\n' "${HX_SRCS[@]}"      | xargs -P "$J" -I{} bash -c 'compile "$1" "'"$AOBJ"'" '"$APP_INC"''   _ {}
printf '%s\n' "${QUIRC_SRCS[@]}"   | xargs -P "$J" -I{} bash -c 'compile "$1" "'"$AOBJ"'" '"$APP_INC"''   _ {}
printf '%s\n' "${MD_SRCS[@]}"      | xargs -P "$J" -I{} bash -c 'compile "$1" "'"$AOBJ"'" '"$APP_INC"''   _ {}
printf '%s\n' "${FONT_SRCS[@]}"    | xargs -P "$J" -I{} bash -c 'compile "$1" "'"$AOBJ"'" '"$APP_INC"''   _ {}
printf '%s\n' "${APP_SRCS[@]}"     | xargs -P "$J" -I{} bash -c 'compile "$1" "'"$AOBJ"'" '"$APP_INC"''   _ {}
# Harness TUs are always rebuilt: this script tracks no header deps, and these
# are the files under active edit.
for s in "${HARNESS_SRCS[@]}"; do rm -f "$AOBJ/$(echo "$s" | tr '/' '_').o"; done
printf '%s\n' "${HARNESS_SRCS[@]}" | xargs -P "$J" -I{} bash -c 'compile "$1" "'"$AOBJ"'" '"$APP_INC"''   _ {}

# Drop objects the current source set no longer contains. The link below globs
# "$AOBJ"/*.o, so an object compiled by an EARLIER version of this script (or by
# a widget-tier harness that once shared this directory) keeps being linked long
# after its source left the list -- and the way that surfaces is a wall of
# "duplicate symbol" errors naming a file nobody is building any more.
{
  printf '%s\n' "${LVGL_SRCS[@]}" "${HX_SRCS[@]}" "${QUIRC_SRCS[@]}" "${MD_SRCS[@]}" \
                 "${FONT_SRCS[@]}" "${APP_SRCS[@]}" "${HARNESS_SRCS[@]}" \
    | tr '/' '_' | sed 's/$/.o/' | sort -u
} > "$AOBJ/.expected"
for o in "$AOBJ"/*.o; do
  grep -qxF "$(basename "$o")" "$AOBJ/.expected" ||
    { echo "  stale, removing: $(basename "$o")"; rm -f "$o" "${o%.o}.d"; }
done
rm -f "$AOBJ/.expected"

# ---- link --------------------------------------------------------------------
# ui_xml/ and assets/ are preloaded into MEMFS at "/", which is what
# helix::set_asset_root("/") in app_main.cpp expects. LV_USE_FS_POSIX maps drive
# 'A' onto that, so asset_component_uri() resolves untouched.
echo "Linking -> $OUT ..."
em++ -O1 -std=c++17 $DEF $APP_INC -fexceptions -sUSE_SDL=2 -sUSE_ZLIB=1 \
  "$LOBJ"/*.o "$AOBJ"/*.o \
  --preload-file ui_xml@/ui_xml \
  --preload-file assets/config@/assets/config \
  --preload-file assets/images@/assets/images \
  --preload-file assets/filaments.json@/assets/filaments.json \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sEXIT_RUNTIME=0 -sSTACK_SIZE=33554432 \
  -sASSERTIONS=1 \
  -sEXPORTED_FUNCTIONS='["_main","_helix_ctl"]' \
  -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","stringToNewUTF8"]' \
  -o "$OUT"
echo "BUILD_OK -> $OUT"
ls -la "$(dirname "$OUT")"
