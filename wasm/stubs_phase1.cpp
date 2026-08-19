// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 1 stub layer: the handful of app-side symbols the filament_path_canvas
// widget references, provided as thin no-ops / fixed values so the widget can be
// built standalone. Token colours are the REAL values from ui_xml/globals.xml
// (Nord-based) so the render is faithful. Phase 2 replaces these with the real
// theme_manager + a mock-subject seeder.

#include "display_settings_manager.h"
#include "memory_utils.h"
#include "settings_manager.h"
#include "theme_manager.h"
#include "ui/ams_drawing_utils.h"
#include "ui_fonts.h"

#include "lvgl/lvgl.h"

#include <string>

// ---- theme_manager (global namespace) ----
lv_color_t theme_manager_get_color(const char* base_name) {
    const std::string n = base_name ? base_name : "";
    // Filament path decorative tokens (dark-mode values from globals.xml).
    if (n == "filament_idle_dark" || n == "filament_idle") return lv_color_hex(0x4c566a);
    if (n == "filament_idle_light") return lv_color_hex(0xd8dee9);
    if (n == "filament_error") return lv_color_hex(0xbf616a);
    if (n == "filament_hub_bg_dark" || n == "filament_hub_bg") return lv_color_hex(0x3b4252);
    if (n == "filament_hub_bg_light") return lv_color_hex(0xe5e9f0);
    if (n == "filament_hub_border_dark" || n == "filament_hub_border") return lv_color_hex(0x4c566a);
    if (n == "filament_hub_border_light") return lv_color_hex(0xd8dee9);
    // Semantic roles (Nord defaults).
    if (n == "text") return lv_color_hex(0xeceff4);
    if (n == "card_bg") return lv_color_hex(0x3b4252);
    if (n == "success") return lv_color_hex(0xa3be8c);
    return lv_color_hex(0x8891a0);
}

int32_t theme_manager_get_spacing(const char* token) {
    const std::string n = token ? token : "";
    if (n == "space_xs") return 4;
    if (n == "space_md") return 10;
    return 6;
}

const lv_font_t* theme_manager_get_font(const char* /*token*/) {
    return &noto_sans_12; // built from assets/fonts/noto_sans_12.c
}

bool theme_manager_is_dark_mode() { return true; }

// ---- ams_draw colour math (real implementations; pure) ----
namespace ams_draw {
lv_color_t lighten_color(lv_color_t c, uint8_t amount) { return lv_color_lighten(c, amount); }
lv_color_t darken_color(lv_color_t c, uint8_t amount) { return lv_color_darken(c, amount); }
lv_color_t blend_color(lv_color_t c1, lv_color_t c2, float factor) {
    if (factor < 0.0f) factor = 0.0f;
    if (factor > 1.0f) factor = 1.0f;
    return lv_color_mix(c2, c1, static_cast<uint8_t>(factor * 255.0f));
}
} // namespace ams_draw

// ---- settings singletons (fixed values; methods don't read members) ----
namespace helix {
SettingsManager& SettingsManager::instance() {
    alignas(SettingsManager) static unsigned char storage[sizeof(SettingsManager)];
    return *reinterpret_cast<SettingsManager*>(storage);
}
ToolheadStyle SettingsManager::get_effective_toolhead_style() const { return ToolheadStyle::DEFAULT; }
} // namespace helix

namespace helix {
DisplaySettingsManager& DisplaySettingsManager::instance() {
    alignas(DisplaySettingsManager) static unsigned char storage[sizeof(DisplaySettingsManager)];
    return *reinterpret_cast<DisplaySettingsManager*>(storage);
}
bool DisplaySettingsManager::get_animations_enabled() const { return true; }
} // namespace helix

// ---- memory info (report a roomy, non-constrained device) ----
namespace helix {
MemoryInfo get_system_memory_info() {
    MemoryInfo m;
    m.total_kb = 8ULL * 1024 * 1024;
    m.available_kb = 4ULL * 1024 * 1024;
    return m;
}
} // namespace helix
