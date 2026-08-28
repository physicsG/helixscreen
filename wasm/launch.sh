#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Launch the LVGL/WASM mockup: build the chosen harness, then serve it so you can
# open it in a browser. WASM needs http:// (file:// is blocked), so this serves
# over a local HTTP server and prints the URL.
#
#   wasm/launch.sh                 # build + serve the REAL AMS pages (default)
#   wasm/launch.sh app 8080        # explicit harness + port
#   wasm/launch.sh ace             # the hand-built ACE mockup (widget-level)
#   wasm/launch.sh widget          # a single filament_path_canvas
#   wasm/launch.sh smoke           # the Phase 0 LVGL smoke test
#   wasm/launch.sh --no-build      # serve the last build without rebuilding
#
set -euo pipefail
cd "$(dirname "$0")/.."   # worktree root

HARNESS="app"
PORT=8080
BUILD=1
for arg in "$@"; do
  case "$arg" in
    app|ace|widget|smoke)  HARNESS="$arg" ;;
    --no-build)        BUILD=0 ;;
    [0-9]*)            PORT="$arg" ;;
    *) echo "usage: wasm/launch.sh [app|ace|widget|smoke] [port] [--no-build]"; exit 1 ;;
  esac
done

case "$HARNESS" in
  app)    OUTDIR="wasm/out_app";    BUILDER="wasm/build_app.sh";    BUILD_ARGS="$OUTDIR/index.html" ;;
  ace)    OUTDIR="wasm/out_ace";    BUILDER="wasm/build_widget.sh"; BUILD_ARGS="wasm/ace_main.cpp $OUTDIR/index.html" ;;
  widget) OUTDIR="wasm/out_widget"; BUILDER="wasm/build_widget.sh"; BUILD_ARGS="wasm/widget_main.cpp $OUTDIR/index.html" ;;
  smoke)  OUTDIR="wasm/out";        BUILDER="wasm/build.sh";        BUILD_ARGS="wasm/smoke_main.cpp $OUTDIR/index.html" ;;
esac

if [[ "$BUILD" == "1" ]]; then
  if ! command -v emcc >/dev/null 2>&1; then
    # shellcheck disable=SC1091
    [[ -f "$HOME/emsdk/emsdk_env.sh" ]] && source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1
  fi
  command -v emcc >/dev/null 2>&1 || { echo "error: emcc not found. Install emsdk (see wasm/README.md)."; exit 1; }
  echo "==> Building '$HARNESS' harness..."
  # shellcheck disable=SC2086
  "$BUILDER" $BUILD_ARGS
fi

[[ -f "$OUTDIR/index.html" ]] || { echo "error: $OUTDIR/index.html not found — build first (drop --no-build)."; exit 1; }

URL="http://localhost:$PORT/index.html"
echo ""
echo "==> Serving $HARNESS mockup at:  $URL"
echo "    (Ctrl-C to stop)"
# Try to open a browser; harmless if none of these exist.
( command -v xdg-open >/dev/null 2>&1 && xdg-open "$URL" \
  || command -v wslview  >/dev/null 2>&1 && wslview "$URL" ) >/dev/null 2>&1 &
cd "$OUTDIR"
exec python3 -m http.server "$PORT" --bind 127.0.0.1
