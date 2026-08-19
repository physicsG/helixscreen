<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# HelixScreen browser preview (LVGL → WebAssembly)

Compile **HelixScreen's real LVGL UI widgets to WebAssembly** and run them in a
browser. This is a developer tool for iterating on widget visuals and layout —
and for **headless, autonomous screenshotting** — without a device or the full
app. The widgets you see are the *actual production code* (the LVGL 9.5 software
renderer, the real `filament_path_canvas`, the helix-xml engine), driven by a
thin harness `main()` and a small stub layer for the app-side symbols they touch.

> This branch (`tooling/lvgl-web-emulator`) carries **only the tooling** — it
> builds whatever widget code is on the branch it's merged into. Feature branches
> enhance the widgets (e.g. the multiACE distance-proportional filament fill on
> `feat/lvgl-web-preview`); merge this tooling onto them to preview those changes.

---

## Quick start

```bash
# One command: build + serve + open a browser (defaults to the ACE-page demo)
./wasm/launch.sh
#   → http://localhost:8080   (Ctrl-C to stop)

./wasm/launch.sh widget       # single filament_path_canvas widget
./wasm/launch.sh smoke        # bare LVGL smoke test (label + button + arc)
./wasm/launch.sh ace 9000     # explicit harness + port
./wasm/launch.sh --no-build   # serve the last build without rebuilding
```

WASM must be served over `http://` — `file://` and sandboxed iframes block it.
`launch.sh` runs a local `python3 -m http.server` for you.

---

## Prerequisites: Emscripten

The build needs `emcc` on `PATH`. Install once via the canonical emsdk (no sudo):

```bash
git clone --depth 1 https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest
```

`launch.sh` / `build_widget.sh` auto-source `~/emsdk/emsdk_env.sh` if `emcc`
isn't already on `PATH`. (Ubuntu's `apt install emscripten` also works but is
older.) Tested with Emscripten 6.0.7.

---

## Harnesses

Each harness is a small `wasm/*_main.cpp` with a `main()` that inits LVGL + SDL,
builds a UI, and enters `emscripten_set_main_loop(lv_timer_handler, ...)`:

| Harness | File | What it shows |
|---|---|---|
| `smoke` | `smoke_main.cpp` | Bare LVGL (label, button, arc). Proves the toolchain. No app code. |
| `widget` | `widget_main.cpp` | One real `filament_path_canvas` driven by its C API. |
| `ace` | `ace_main.cpp` | A full **interactive ACE page** mockup — slot bays, the real path canvas, an operation sidebar, and a mocked **Load/Unload run** with a step stepper, sensor chips, and nozzle heat glow. |

The `ace` harness exports `ace_load()` / `ace_unload()` / `ace_reset()` via
`EMSCRIPTEN_KEEPALIVE`, so a headless script can drive a run with
`Module._ace_load()` (the on-screen buttons work too).

---

## How the build works (`build_widget.sh`)

`build.sh` builds the `smoke` harness (LVGL only). `build_widget.sh` builds the
widget/ACE harnesses, compiling and linking, in parallel with an object cache:

1. **LVGL** — `lib/lvgl/src/**` **except** the XML engine, bundled expat, and all
   platform display drivers *other than SDL* (they pull Linux-only headers
   Emscripten lacks). Cached in `wasm/obj/` by source mtime.
2. **helix-xml** — `lib/helix-xml/src/**` (our XML engine; WASM-clean C).
3. **The widget TUs + fonts + stub layer** — always recompiled (the script does
   not track header deps, and the widget TUs share `ui_filament_path_internal.h`;
   a stale object with an old struct layout is an ABI mismatch that crashes at
   run time).
4. **Link** the harness `main()` via `emscripten_set_main_loop`.

```bash
./wasm/build_widget.sh wasm/ace_main.cpp wasm/out_ace/index.html
#                       └ harness main    └ output (index.html + .js + .wasm)
```

### The stub layer

The widgets reference a few app-side symbols that don't exist in a bare harness:

- **`helix_stubs.c`** — no-op the `helix_*` hooks that `patches/` bake into the
  LVGL submodule (`helix_lvgl_anomaly`, `helix_notify_app_*`). `extern "C"` so the
  C-compiled LVGL objects resolve them.
- **`stubs_phase1.cpp`** — `theme_manager_*` (returns the *real* token colours,
  read from `ui_xml/globals.xml`, so the render is faithful), `ams_draw` colour
  math (real), and fixed `SettingsManager` / `DisplaySettingsManager` /
  `get_system_memory_info` accessors. Fonts use the built-in Montserrat via the
  WASM `lv_conf.h`.

### `wasm/lv_conf.h`

A WASM-specific copy of the LVGL config with four deltas from the repo default:
`LV_USE_OS = LV_OS_NONE` (single-threaded), a built-in default font (no external
`noto_sans_14`), a plain `abort()` assert handler, and
`LV_SDL_MOUSEWHEEL_MODE = ENCODER` (the `CROWN` path has an upstream `dsc` bug).

---

## Headless screenshots (autonomous verification)

`pw_shot.py` (single frame) and `ace_shot.py` (idle → mid-load → loaded) use
**Playwright/Chromium** to render the served page and screenshot it — how the UI
is verified without a human at a browser.

```bash
python3 -m venv /tmp/pwvenv && /tmp/pwvenv/bin/pip install playwright
/tmp/pwvenv/bin/playwright install chromium
# serve first (launch.sh), then:
/tmp/pwvenv/bin/python wasm/pw_shot.py http://127.0.0.1:8080/index.html /tmp/shot.png
```

Headless Chromium needs a few system libs (`libnss3`, `libnspr4`). **Without
sudo**, fetch and unpack them locally and point `LD_LIBRARY_PATH` at them:

```bash
mkdir -p /tmp/chromelibs && cd /tmp/chromelibs
apt-get download libnss3 libnspr4 && for d in *.deb; do dpkg-deb -x "$d" .; done
export LD_LIBRARY_PATH=/tmp/chromelibs/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH
```
The screenshot scripts launch Chromium with `--use-angle=swiftshader` so WebGL
works headless.

---

## Adding a new harness (preview any widget)

1. Write `wasm/mywidget_main.cpp`: `lv_init()`, `lv_sdl_window_create(w,h)`,
   `lv_sdl_mouse_create()`, create your widget on `lv_screen_active()`, drive its
   C API, then `emscripten_set_main_loop(lv_timer_handler, 0, 1)`.
2. If it pulls in widget TUs beyond the filament path, add them to `WIDGET_SRCS`
   in `build_widget.sh`, and stub any new app-side symbols the linker reports
   (extend `stubs_phase1.cpp`) — the linker names exactly what's missing.
3. Build + serve: `./wasm/build_widget.sh wasm/mywidget_main.cpp wasm/out_x/index.html`
   then `(cd wasm/out_x && python3 -m http.server 8080)`.

---

## Limitations & notes

- **Single-threaded** (`LV_OS_NONE`); no networking, no real filesystem, no
  backend. Harnesses drive widgets directly via their C API.
- **Stubbed theme** — token colours are the real values but the full theme
  engine (18 JSON themes, breakpoints) is not compiled in. A future "storybook"
  tier would compile the real `theme_manager` + widget factories and render
  `ui_xml/components/*.xml` with mock subjects (see the design proposal that
  motivated this tooling).
- **Widget carve-out, not the whole app.** Booting the full app in WASM is a
  much larger effort (threads/sockets/audio/config to gate); out of scope here.
- Animation callbacks invalidate **directly** (not via `lv_async_call`, which
  coalesces under a busy main loop and starved per-frame redraws) — relevant if
  you add animated widgets.
