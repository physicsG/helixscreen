// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Browser entry point for the REAL HelixScreen AMS pages.
//
// Mirrors the desktop startup phases (src/application/application.cpp
// Application::run()) with the real registration/init calls, minus the CLI,
// splash, wizard, plugin and network seams -- the same shape as the ESP32 port's
// components/helixapp/app_boot.cpp, which solved this problem once already for a
// target with no Linux underneath it.
//
// The panels, the XML, the theme, the subject graph and the AMS backends here
// are all production code. The only thing that is fake is the printer.

#include "ui_ams_mini_status.h"
#include "ui_bed_mesh.h"
#include "ui_card.h"
#include "ui_component_header_bar.h"
#include "ui_dialog.h"
#include "ui_gradient_canvas.h"
#include "ui_icon.h"
#include "ui_nav_manager.h"
#include "ui_panel_ams_overview.h"
#include "ui_severity_card.h"
#include "ui_status_pill.h"
#include "ui_switch.h"
#include "ui_temp_display.h"
#include "ui_update_queue.h"

#include "ams_backend_mock.h"
#include "ams_state.h"
#include "app_globals.h"
#include "asset_manager.h"
#include "config.h"
#include "data_root_resolver.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "helix_sparkline.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "panel_factory.h"
#include "printer_state.h"
#include "remote_control_server.h"
#include "runtime_config.h"
#include "setting_group.h"
#include "subject_initializer.h"
#include "theme_manager.h"
#include "translation_loader.h"
#include "xml_registration.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <emscripten.h>
#include <memory>
#include <string>
#include <sys/stat.h>

namespace {

constexpr int SCREEN_W = 800;
constexpr int SCREEN_H = 480;

lv_display_t* g_display = nullptr;
lv_obj_t* g_screen = nullptr;
lv_obj_t* g_app_layout = nullptr;
std::unique_ptr<SubjectInitializer> g_subjects;
std::unique_ptr<helix::PanelFactory> g_panels;

void main_loop() {
    lv_timer_handler();
}

/// Phase 2: the writable half of MEMFS plus the read-only preload root.
/// --preload-file mounts ui_xml/ and assets/ at "/", which is exactly what
/// set_asset_root("/") makes asset_component_uri() resolve against; LV_USE_FS_POSIX
/// maps LVGL drive 'A' onto the same tree, so no path in the app changes.
void init_filesystem() {
    ::mkdir("/config", 0755);
    helix::set_asset_root("/");
    helix::Config::get_instance()->init("/config/settings.json");
}

/// Phase 4/6: fonts and images must be registered before globals.xml is parsed
/// (it references font tokens), and the theme after it (it reads the constants).
bool init_theme() {
    spdlog::debug("[WASM] phase: assets");
    AssetManager::register_all();
    spdlog::debug("[WASM] phase: globals.xml");

    if (lv_xml_register_component_from_file(
            helix::asset_component_uri("ui_xml/globals.xml").c_str()) != LV_RESULT_OK) {
        spdlog::error("[WASM] FATAL: globals.xml did not load — every XML constant is missing");
        return false;
    }
    spdlog::debug("[WASM] phase: theme_manager_init");
    theme_manager_init(g_display, /*dark_mode=*/true);
    spdlog::debug("[WASM] phase: theme ready");
    spdlog::info("[WASM] globals.xml registered");
    theme_manager_apply_bg_color(g_screen, "screen_bg", LV_PART_MAIN);
    return true;
}

/// Phase 7: the custom widgets, registered BEFORE any XML that uses them.
/// Mirrors Application::register_widgets(). Skipping it is not subtle but it is
/// silent: the XML engine logs one "not a known widget" error per tag and
/// re-parents the children, which turns into runaway recursion by the time
/// app_layout is done.
void register_widgets() {
    ui_icon_register_widget();
    ui_status_pill_register_widget();
    ui_switch_register();
    ui_card_register();
    setting_group_register();
    ui_temp_display_init();
    ui_ams_mini_status_init();
    ui_severity_card_register();
    ui_dialog_register();
    ui_bed_mesh_register();
    ui_gradient_canvas_register();
    helix::ui::register_helix_sparkline_widget();
    ui_component_header_bar_init();
}

/// Phase 8a: lv_tr() has to work before any XML is parsed.
void init_translations() {
    spdlog::debug("[WASM] phase: translations");
    helix::ui::ensure_translation_loaded("en");
    lv_translation_set_language("en");
}

/// Phase 9a-9c + the AMS backend. The API pointer stays null: the AMS backends
/// tolerate it (there are 8 `api_->` uses across all three, all on REST paths we
/// never reach), and nothing else on these screens sends a command.
void init_state() {
    g_subjects = std::make_unique<SubjectInitializer>();
    spdlog::debug("[WASM] phase: init_core_and_state");
    g_subjects->init_core_and_state();
    spdlog::debug("[WASM] phase: core state done");

    auto mock = std::make_unique<AmsBackendMock>(4);
    mock->set_multiace_mode(true);
    mock->start();
    AmsState::instance().set_backend(std::move(mock));
    spdlog::info("[WASM] AMS backend installed: Snapmaker U1 + 2x ACE");

    spdlog::debug("[WASM] phase: init_panels");
    g_subjects->init_panels(nullptr, *get_runtime_config());
    // init_post() is skipped for now: its USB phase constructs UsbManager, whose
    // mock backend spawns a std::thread, and a non-pthread WASM build aborts on
    // the first thread creation. Nothing on the AMS pages observes it. See the
    // threading note in wasm/README.md.
    spdlog::debug("[WASM] phase: subjects done (init_post skipped)");
}

/// The synthetic printer. Everything the AMS pages read about the machine —
/// whether the controls are enabled at all, what the nozzle is doing — comes
/// from PrinterState, and PrinterState is fed by exactly one production entry
/// point. So this drives that entry point instead of reaching into subjects:
/// same path a real notify frame takes, zero network. Copied in shape from the
/// ESP32 port's mock driver (components/helixapp/app_boot.cpp).
void seed_printer_state() {
    auto& ps = get_printer_state();
    ps.set_printer_connection_state(2 /*CONNECTED*/, "browser preview");
    ps.set_klippy_state(helix::KlippyState::READY);

    // Without a ready printer the AMS content renders disabled: ams_panel.xml
    // and ams_overview_panel.xml both carry
    // <bind_state_if_not_eq subject="nav_buttons_enabled" state="disabled" .../>.
    lv_timer_create(
        [](lv_timer_t*) {
            static int t = 0;
            ++t;
            const double nozzle = 214.0 + (t % 3);
            const double bed = 58.0 + (t % 5);
            nlohmann::json status = {
                {"extruder", {{"temperature", nozzle}, {"target", 215.0}}},
                {"heater_bed", {{"temperature", bed}, {"target", 60.0}}},
            };
            get_printer_state().update_from_status(status);
        },
        1000, nullptr);
}

/// Phase 10: the shell. app_layout.xml carries the navbar and the panel
/// container every panel is found inside.
bool init_ui() {
    g_app_layout = static_cast<lv_obj_t*>(lv_xml_create(g_screen, "app_layout", nullptr));
    if (!g_app_layout) {
        spdlog::error("[WASM] FATAL: app_layout XML create failed");
        return false;
    }
    lv_obj_remove_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_update_layout(g_screen);

    NavigationManager::instance().set_app_layout(g_app_layout);
    NavigationManager::instance().init_overlay_backdrop(g_screen);

    lv_obj_t* navbar = lv_obj_find_by_name(g_app_layout, "navbar");
    lv_obj_t* content_area = lv_obj_find_by_name(g_app_layout, "content_area");
    if (!navbar || !content_area) {
        spdlog::error("[WASM] FATAL: navbar/content_area missing from app_layout");
        return false;
    }
    NavigationManager::instance().wire_events(navbar);

    lv_obj_t* panel_container = lv_obj_find_by_name(content_area, "panel_container");
    if (!panel_container) {
        spdlog::error("[WASM] FATAL: panel_container missing");
        return false;
    }
    g_panels = std::make_unique<helix::PanelFactory>();
    if (!g_panels->find_panels(panel_container)) {
        return false;
    }
    g_panels->setup_panels(g_screen);

    // Publish the initial panel. Every panel's visibility is an XML binding on
    // the active_panel subject, so without this the container renders with all
    // six children hidden -- a navbar over an empty content area.
    NavigationManager::instance().set_active(helix::PanelId::Home);
    return true;
}

} // namespace

// =============================================================================
// helix_ctl — the `helix-screen ctl` command surface, in the browser.
//
// Same JSON-RPC vocabulary as the desktop tool (navigate, click, ls, text,
// geom, set_value, screenshot...), same widget locators, because it is the same
// dispatcher: only the transport differs. From JS:
//
//   Module.ccall('helix_ctl','string',['string'],
//     [JSON.stringify({jsonrpc:'2.0',id:1,method:'click',
//                      params:{name:'ams_unit_card[1]'}})])
//
// The returned pointer is owned by this function and stays valid until the next
// call, which is all ccall's 'string' return type needs.
// =============================================================================
extern "C" EMSCRIPTEN_KEEPALIVE const char* helix_ctl(const char* request) {
    static std::string response;
    try {
        response = helix::RemoteControlServer::instance().serve_inproc(request ? request : "");
    } catch (const std::exception& e) {
        response = std::string("{\"error\":\"") + e.what() + "\"}";
    }
    return response.c_str();
}

int main(int, char**) {
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%^%l%$] %v");
    spdlog::info("[WASM] HelixScreen browser preview starting");

    get_runtime_config()->test_mode = true; // engages the mock arms app-wide

    init_filesystem();

    lv_init();
    // The XML engine keeps its own registries (components, fonts, images,
    // subjects) and they do not exist until this runs. Desktop gets it from
    // init_lvgl() in src/lvgl_init.cpp; skipping it here left every
    // lv_xml_register_* call with nowhere to put the entry.
    lv_xml_init();
    g_display = lv_sdl_window_create(SCREEN_W, SCREEN_H);
    lv_sdl_mouse_create();
    lv_sdl_keyboard_create();
    g_screen = lv_screen_active();

    // Installs the 1 ms LVGL timer that drains UpdateQueue. Without it every
    // queue_update()/defer() in the app is queued and never runs -- including
    // the deferred unhide inside NavigationManager::push_overlay(), which is
    // how "the panel is built but invisible" presents.
    helix::ui::update_queue_init();
    spdlog::info("[WASM] LVGL + SDL up ({}x{})", SCREEN_W, SCREEN_H);

    if (!init_theme()) {
        return 1;
    }
    init_translations();
    spdlog::debug("[WASM] phase: register_widgets");
    register_widgets();
    spdlog::debug("[WASM] phase: register_xml_components");
    helix::register_xml_components();
    spdlog::debug("[WASM] phase: xml registered");
    spdlog::debug("[WASM] phase: state");
    init_state();
    seed_printer_state();
    spdlog::debug("[WASM] phase: shell");
    if (!init_ui()) {
        return 1;
    }

    navigate_to_ams_panel();

    // Deterministic readiness flag for drivers: polling ccall('helix_ctl') before
    // the module has initialised trips an Emscripten assertion, so scripts wait
    // on this instead.
    EM_ASM({ window.helixReady = true; });
    spdlog::info("[WASM] Boot complete — AMS pages live");

    // simulate_infinite_loop = 0: main() returns and the runtime stays alive
    // (EXIT_RUNTIME=0). The `1` form unwinds main with a JS exception, which
    // leaves the wasm stack pointer mid-frame -- every later ccall into
    // helix_ctl then trapped on stackRestore.
    emscripten_set_main_loop(main_loop, 0, 0);
    return 0;
}
