<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# HelixScreen browser preview (LVGL → WebAssembly)

Compile **HelixScreen to WebAssembly** and run it in a browser — for iterating on
UI without a device, and for **headless, autonomous screenshotting**.

Two tiers live here:

- **`app`** (default) — the **real AMS pages**: production XML, production
  panels, the real `AmsState` and AMS backends, the real theme and subject
  graph, booted by `app_main.cpp` the way `Application::run()` boots the
  desktop. Only the printer is fake.
- **`widget` / `ace` / `smoke`** — single-widget harnesses that drive one
  production widget through its C API against a small stub layer. Faster to
  build, useful when iterating on one canvas.

> This branch (`tooling/lvgl-web-emulator`) carries **only the tooling** — it
> builds whatever widget code is on the branch it's merged into. Feature branches
> enhance the widgets (e.g. the multiACE distance-proportional filament fill on
> `feat/lvgl-web-preview`); merge this tooling onto them to preview those changes.

---

## Quick start

```bash
# One command: build + serve + open a browser (defaults to the real AMS pages)
./wasm/launch.sh
#   → http://localhost:8080   (Ctrl-C to stop)

./wasm/launch.sh ace          # hand-built ACE mockup (widget tier)
./wasm/launch.sh widget       # single filament_path_canvas widget
./wasm/launch.sh smoke        # bare LVGL smoke test (label + button + arc)
./wasm/launch.sh app 9000     # explicit harness + port
./wasm/launch.sh --no-build   # serve the last build without rebuilding
```

The first `app` build is ~15 min (600 app TUs + LVGL); after that the object
cache makes an `app_main.cpp` edit a relink only.

WASM must be served over `http://` — `file://` and sandboxed iframes block it.
`launch.sh` runs a local `python3 -m http.server` for you.

---

## Prerequisites: Emscripten

The build needs `emcc` on `PATH`. Install once via the canonical emsdk (no sudo):

```bash
git clone --depth 1 https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest
```

`launch.sh` / `build_app.sh` / `build_widget.sh` auto-source `~/emsdk/emsdk_env.sh` if `emcc`
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
| `ace` | `ace_main.cpp` | A hand-built **ACE page** mockup — slot bays, the real path canvas, an operation sidebar, and a mocked **Load/Unload run** with a step stepper, sensor chips, and nozzle heat glow. Predates the `app` tier; keep for widget-level iteration. |
| `app` | `app_main.cpp` | **The real thing.** Boots the production app far enough to render the three Snapmaker U1 AMS screens, driven by `AmsBackendMock` in multiACE mode. See below. |

The `ace` harness exports `ace_load()` / `ace_unload()` / `ace_reset()` via
`EMSCRIPTEN_KEEPALIVE`, so a headless script can drive a run with
`Module._ace_load()` (the on-screen buttons work too).

---

## The `app` harness — real pages in a browser

### What it renders

Three screens, all modes of one overlay (`AmsOverviewPanel`), reached exactly as
they are on a device:

| Screen | State | Reached by |
|---|---|---|
| Multi-Filament | `detail_unit_index_ = -1` | boot (`navigate_to_ams_panel()`) |
| SnapSwap | `= 0`, PARALLEL | tap `ams_unit_card[0]` |
| multiACE | `= 1`, HUB | tap `ams_unit_card[1]` |

### How it boots (`app_main.cpp`)

Mirrors `Application::run()`'s phase order, which is a contract, not a
suggestion — the subject graph and the theme have to exist before any XML is
parsed. Modelled on the ESP32 port's `components/helixapp/app_boot.cpp`, which
solved the same problem for a target with no Linux underneath it.

MEMFS layout: `--preload-file` mounts `ui_xml/` and `assets/` at `/`, which is
what `helix::set_asset_root("/")` resolves against; `LV_USE_FS_POSIX` maps LVGL
drive `A` onto the same tree, so `asset_component_uri()` needs no changes.

Four calls are easy to omit and each fails in a way that does not name itself:

| Call | Symptom if missing |
|---|---|
| `lv_xml_init()` | Every `lv_xml_register_*` warns "No component found", then the first file registration hangs |
| `register_widgets()` (Phase 7) | "XML tag 'ui_card' is not a known widget", children re-parent, runaway recursion in `app_layout` |
| `helix::ui::update_queue_init()` | Panels build correctly and stay invisible — the deferred unhide inside `push_overlay()` never runs |
| `NavigationManager::set_active(PanelId::Home)` | Navbar over an empty content area: panel visibility is an XML binding on `active_panel` |

### Driving it — `helix_ctl`

The page exports the **`helix-screen ctl` command surface**: same JSON-RPC
vocabulary, same widget locators, same dispatcher — only the transport differs
(`RemoteControlServer::serve_inproc()` instead of a Unix socket).

```js
Module.ccall('helix_ctl', 'string', ['string'],
  [JSON.stringify({jsonrpc:'2.0', id:1, method:'click',
                   params:{name:'ams_unit_card[1]'}})])
```

`wasm/ctl_shot.py` uses it to walk all three screens and screenshot each:

```bash
./wasm/launch.sh &                                   # serve on :8080
/tmp/pwvenv/bin/python wasm/ctl_shot.py http://127.0.0.1:8080/index.html /tmp/shots
```

Scripts should wait on `window.helixReady === true` before the first call —
polling `ccall` before the module initialises trips an Emscripten assertion.

### Known issues

- **`resolve`/`text`/`geom` by an ambiguous bare name traps the module.**
  `header_title` exists in every panel; resolving it kills the wasm instance and
  every later call reports "UI update queue did not drain" (the module is gone,
  not the queue). Address widgets by full `path` until this is fixed. Names
  unique to one subtree (`ams_unit_card[0]`, `unit_name[0]`) are fine.
- **`init_post()` is skipped.** Its USB phase constructs `UsbManager`, whose mock
  backend spawns a `std::thread`; a non-pthread WASM build aborts on the first
  thread creation. Nothing on the AMS pages observes it. Enabling `-pthread`
  would need COOP/COEP headers from the server.
- **The backend is `AmsBackendMock`, not a recorded device.** Good enough to
  render the screens; a `ScriptedU1` over the real `AmsBackendMultiAce`, replaying
  captured status frames, is the next step — see the design notes.

### Build notes that are not optional

- `-include include/lvgl_pch.h` on **C++ TUs only**. The native build force-feeds
  this PCH to every TU and it is where most files get their `lv_xml_*`
  declarations and `hv/json.hpp`. A `.c` TU cannot parse it.
- `-fexceptions` on compile **and** link. Emscripten compiles `try`/`catch` away
  by default, so a `throw` traps as `unreachable` — and nlohmann::json throws on
  every type mismatch.
- `emscripten_set_main_loop(fn, 0, 0)` — the `1` form unwinds `main()` with a JS
  exception and leaves the wasm stack pointer mid-frame, after which every
  `ccall` into `helix_ctl` traps on `stackRestore`.
- `-isystem lib/libhv/cpputil` — where `json.hpp` actually lives.

### The source manifest

`wasm/app_srcs.txt` is ~600 app TUs, **derived rather than curated**: the
transitive link closure of the AMS panel objects taken from a native build with
`nm`, filtered to what compiles under `emcc`, plus the boot path. `stubs_app.cpp`
holds the entire remainder — libhv's synchronous HTTP client and its logger, the
wpa_supplicant control socket — none of which the AMS pages ever call.

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
