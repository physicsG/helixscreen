#!/bin/bash

# Copyright (C) 2025-2026 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later

set -e

# Show help
show_help() {
    cat << 'EOF'
Usage: screenshot.sh [BINARY] [NAME] [TOKEN] [FLAGS...]

Capture a screenshot of the HelixScreen UI, driven by helix-screen ctl.

Launches the binary with its remote-control server on a private socket, drives
the UI to the requested screen with a navigation recipe, captures a screenshot,
converts it to PNG, and shuts the instance down. Each capture is an isolated,
freshly-booted process — no state leaks between shots.

Arguments:
  BINARY    Binary name in build/bin/ (default: helix-screen)
  NAME      Output filename suffix (default: timestamp)
            Screenshot saved to: /tmp/ui-screenshot-<NAME>.png
  TOKEN     Screen to capture (optional; default: home). May be a base panel
            (home, controls, filament, settings, advanced, print-select), an
            overlay (motion, bed-mesh, network, zoffset, ...), or a sample-data
            screen (preflight-check, runout-modal, lock-screen, print-status,
            print-tune). See scripts/screenshot-recipes.sh for the full list.
            An unknown token is tried as a bare `navigate <token>`.
  FLAGS     Additional flags passed to the binary (e.g., --dark,
            -s 800x480, --layout ultrawide). Pass --wizard to capture the
            first-run wizard (suppresses --skip-wizard).

            Captures run against mock data (--test) by default. Pass --real to
            capture against the configured printer instead — that runs the app
            in production mode, which rewrites your real config/settings.json
            and ~/.helixscreen/*.backup, so it is opt-in.

            --recipe '<steps>'  Drive the UI with these `helix-screen ctl`
            steps (semicolon-separated) instead of a recipe-table lookup —
            for a screen with no table entry yet. Overrides TOKEN's recipe.

Environment Variables:
  HELIX_SCREENSHOT_DISPLAY   Display index to open the window on (default: auto)
  HELIX_SCREENSHOT_TIMEOUT   Max seconds to wait for the control socket (default: 20)
  HELIX_SCREENSHOT_DELAY     Settle seconds after the recipe before capture (default: 1.5)
  HELIX_SCREENSHOT_OPEN      If set, opens the screenshot in a viewer

Examples:
  ./scripts/screenshot.sh                                 # default binary, home
  ./scripts/screenshot.sh helix-screen home-panel home
  ./scripts/screenshot.sh helix-screen motion motion -s small
  ./scripts/screenshot.sh helix-screen zoffset zoffset
  ./scripts/screenshot.sh helix-screen preflight preflight-check
  ./scripts/screenshot.sh helix-screen wizard-wifi "" --wizard
  ./scripts/screenshot.sh helix-screen safety "" \
      --recipe 'navigate settings; click row_safety'
  ./scripts/screenshot.sh helix-screen live-home home --real   # real printer

Output:
  Screenshots are saved to /tmp/ui-screenshot-<NAME>.png, encoded by the app.

Dependencies:
  - none beyond the built binary (PNG is encoded in-app via lodepng)
EOF
    exit 0
}

case "${1:-}" in
    -h|--help|help) show_help ;;
esac

# Colors
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
info() { echo -e "${BLUE}ℹ${NC} $1"; }
success() { echo -e "${GREEN}✓${NC} $1"; }
warn() { echo -e "${YELLOW}⚠${NC} $1"; }
error() { echo -e "${RED}✗${NC} $1"; }

# Project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# shellcheck source=screenshot-recipes.sh
source "$SCRIPT_DIR/screenshot-recipes.sh"

BINARY="${1:-helix-screen}"
BINARY_PATH="./build/bin/${BINARY}"
HELIXCTL=("./build/bin/helix-screen" ctl)

NAME="${2:-$(date +%s)}"
BMP_FILE="/tmp/ui-screenshot-${NAME}.bmp"
PNG_FILE="/tmp/ui-screenshot-${NAME}.png"

# Third arg: TOKEN (a screen) unless it starts with '-', in which case it's a flag.
TOKEN=""
if [ $# -ge 3 ]; then
    if [[ "${3}" == -* ]]; then
        shift 2; EXTRA_ARGS=("$@")
    else
        TOKEN="${3}"; shift 3 2>/dev/null || true; EXTRA_ARGS=("$@")
    fi
else
    shift 2 2>/dev/null || true; EXTRA_ARGS=("$@")
fi

# --recipe '<ctl steps>' captures a screen with no table entry, without having to
# add one first. Pull it (and its value) out of the flags forwarded to the binary.
INLINE_RECIPE=""
FILTERED_ARGS=()
skip_next=0
for a in "${EXTRA_ARGS[@]}"; do
    if [ "$skip_next" = "1" ]; then INLINE_RECIPE="$a"; skip_next=0; continue; fi
    if [ "$a" = "--recipe" ]; then skip_next=1; continue; fi
    FILTERED_ARGS+=("$a")
done
EXTRA_ARGS=("${FILTERED_ARGS[@]}")

# Wizard capture: --wizard is forwarded to the binary, where it sets force_wizard
# and overrides the --skip-wizard that --test otherwise implies. We also withhold
# our own --skip-wizard and run no recipe (we just capture the boot screen).
WIZARD_MODE=0
for a in "${EXTRA_ARGS[@]}"; do
    [ "$a" = "--wizard" ] && WIZARD_MODE=1
done

# Mock by default. Without --test the app runs in PRODUCTION mode: it reads and
# rewrites the developer's real config/settings.json, drops tool_spools.json and
# telemetry_queue.json into the repo, and — because /var/lib/helixscreen is
# root-owned — falls through to rewriting ~/.helixscreen/*.backup. Screenshots
# are overwhelmingly taken against mock data, so that has to be opt-in, not the
# accident you get by forgetting a flag. --real captures against the configured
# printer instead; it is consumed here and never forwarded to the binary.
REAL_MODE=0
FILTERED_ARGS=()
for a in "${EXTRA_ARGS[@]}"; do
    if [ "$a" = "--real" ]; then REAL_MODE=1; continue; fi
    [ "$a" = "--test" ] && REAL_MODE=0
    FILTERED_ARGS+=("$a")
done
EXTRA_ARGS=("${FILTERED_ARGS[@]}")

# Headless: no display server at all (CI, ssh session, container). SDL's dummy
# video driver needs no window system, and the SDL backend falls back to the
# software renderer on its own. Screenshots still come out correct because
# capture goes through lv_snapshot_take(), which re-renders the object tree
# into its own buffer rather than reading back the display. HELIX_HEADLESS=1
# forces this on a machine that does have a display, so it is checked before the
# Wayland auto-detect below — otherwise that branch would claim SDL_VIDEODRIVER
# first and the request would be silently ignored.
#
# No need to also export SDL_AUDIODRIVER=dummy here: the binary's
# silence_audio_if_headless() in main() notices SDL_VIDEODRIVER=dummy and
# forces it itself, so a screenshot sweep doesn't beep on every panel change.
if [ -z "$SDL_VIDEODRIVER" ] && { [ "${HELIX_HEADLESS:-0}" = "1" ] ||
    { [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; }; }; then
    export SDL_VIDEODRIVER=dummy
    info "Headless — using SDL_VIDEODRIVER=dummy (software renderer)"
fi

# On a Wayland desktop, force SDL's native Wayland driver (avoids XWayland GLX crash).
if [ -n "$WAYLAND_DISPLAY" ] && [ -z "$SDL_VIDEODRIVER" ]; then
    export SDL_VIDEODRIVER=wayland
    info "Wayland session detected — using SDL_VIDEODRIVER=wayland"
fi

# Display index
if [ -z "$HELIX_SCREENSHOT_DISPLAY" ]; then
    if [ -n "$WAYLAND_DISPLAY" ]; then HELIX_SCREENSHOT_DISPLAY=0; else HELIX_SCREENSHOT_DISPLAY=1; fi
fi

SOCKET_TIMEOUT="${HELIX_SCREENSHOT_TIMEOUT:-20}"
SETTLE="${HELIX_SCREENSHOT_DELAY:-1.5}"

# Binary present + executable
if [ ! -f "$BINARY_PATH" ]; then
    error "Binary not found: $BINARY_PATH"; info "Build first with: make"; exit 1
fi
[ -x "$BINARY_PATH" ] || chmod +x "$BINARY_PATH"
if [ ! -x "./build/bin/helix-screen" ]; then
    error "helix-screen not found: ./build/bin/helix-screen"; info "Build it with: make -j"; exit 1
fi

# Private per-invocation socket so we never collide with a dev instance.
SOCK="/tmp/helix-shot-$$.sock"
LOG="/tmp/helix-shot-$$.log"
rm -f "$SOCK" 2>/dev/null || true

HELIX_PID=""
cleanup() {
    if [ -n "$HELIX_PID" ] && kill -0 "$HELIX_PID" 2>/dev/null; then
        # Ask it to exit cleanly (flushes logs, runs shutdown paths); fall back
        # to a signal if the control socket is already gone.
        "${HELIXCTL[@]}" -s "$SOCK" shutdown >/dev/null 2>&1 || kill "$HELIX_PID" 2>/dev/null || true
        for _ in 1 2 3 4 5 6 7 8 9 10; do
            kill -0 "$HELIX_PID" 2>/dev/null || break
            sleep 0.2
        done
        kill -0 "$HELIX_PID" 2>/dev/null && kill "$HELIX_PID" 2>/dev/null || true
    fi
    rm -f "$SOCK" "$LOG" 2>/dev/null || true
}
trap cleanup EXIT

# Assemble launch flags. --skip-splash for speed; --remote for the control server.
LAUNCH_FLAGS=(--remote --remote-socket "$SOCK" --skip-splash
              --display "$HELIX_SCREENSHOT_DISPLAY")
[ "$WIZARD_MODE" = "0" ] && LAUNCH_FLAGS+=(--skip-wizard)
if [ "$REAL_MODE" = "1" ]; then
    warn "Capturing in PRODUCTION mode (--real): this run reads and rewrites your real config."
else
    # Only add --test when the caller did not already pass it explicitly, so the
    # flag never appears twice on the command line.
    printf '%s\n' "${EXTRA_ARGS[@]}" | grep -qx -- '--test' || LAUNCH_FLAGS+=(--test)
fi

info "Launching ${BINARY} (private socket $SOCK)..."
"$BINARY_PATH" "${LAUNCH_FLAGS[@]}" "${EXTRA_ARGS[@]}" > "$LOG" 2>&1 &
HELIX_PID=$!

# Wait for the control socket.
waited=0
while [ ! -S "$SOCK" ]; do
    if ! kill -0 "$HELIX_PID" 2>/dev/null; then
        error "Binary exited before the control socket appeared"
        tail -15 "$LOG" 2>/dev/null
        exit 1
    fi
    if [ "$waited" -ge "$((SOCKET_TIMEOUT * 2))" ]; then
        error "Timed out after ${SOCKET_TIMEOUT}s waiting for control socket"
        exit 1
    fi
    sleep 0.5; waited=$((waited + 1))
done

# Run the navigation recipe (skip in wizard mode — the wizard shows itself).
if [ "$WIZARD_MODE" = "0" ]; then
    if [ -n "$INLINE_RECIPE" ]; then
        RECIPE="$INLINE_RECIPE"
        info "Recipe (inline): $RECIPE"
    else
        RECIPE="$(screenshot_recipe_for "${TOKEN:-home}")"
        info "Recipe: $RECIPE"
    fi
    IFS=';' read -ra STEPS <<< "$RECIPE"
    for step in "${STEPS[@]}"; do
        # trim leading/trailing whitespace
        step="$(echo "$step" | sed 's/^ *//;s/ *$//')"
        [ -z "$step" ] && continue
        # Surface the control server's error text — a silently-skipped step
        # produces a screenshot of the wrong screen, which is worse than a fail.
        if ! STEP_ERR=$("${HELIXCTL[@]}" -s "$SOCK" $step 2>&1 >/dev/null); then
            warn "Recipe step failed: '$step'${STEP_ERR:+ — $STEP_ERR}"
        fi
    done
else
    info "Wizard mode: capturing boot screen (no recipe)"
fi

# Let animations/transitions settle, then capture straight to PNG. The app
# encodes it (lodepng), so there is no BMP hop and no ImageMagick dependency.
sleep "$SETTLE"
if ! CAPTURE_ERR=$("${HELIXCTL[@]}" -s "$SOCK" screenshot "$PNG_FILE" 2>&1 >/dev/null); then
    error "helix-screen ctl screenshot failed${CAPTURE_ERR:+: $CAPTURE_ERR}"
    tail -10 "$LOG" 2>/dev/null
    exit 1
fi
if [ ! -f "$PNG_FILE" ]; then
    error "Screenshot not written: $PNG_FILE"; tail -10 "$LOG" 2>/dev/null; exit 1
fi

PNG_SIZE=$(ls -lh "$PNG_FILE" | awk '{print $5}')
echo ""
success "Screenshot ready!"
echo "  File:  $PNG_FILE ($PNG_SIZE)"
if [ -n "$INLINE_RECIPE" ]; then
    echo "  Recipe: $INLINE_RECIPE"
else
    echo "  Token: ${TOKEN:-home}"
fi
echo ""

if [ -n "$HELIX_SCREENSHOT_OPEN" ]; then
    command -v open &>/dev/null && open "$PNG_FILE" || { command -v xdg-open &>/dev/null && xdg-open "$PNG_FILE"; }
fi
