// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_settings_printing.cpp
 * @brief Implementation of PrintingSettingsOverlay
 */

#include "ui_settings_printing.h"

#include "ui_callback_helpers.h"
#include "ui_overlay_retraction_settings.h"
#include "ui_overlay_timelapse_settings.h"
#include "ui_settings_machine_limits.h"
#include "ui_settings_macro_buttons.h"
#include "ui_settings_material_temps.h"
#include "ui_settings_motion.h"

#include "app_globals.h"
#include "post_op_cooldown_manager.h"
#include "safety_settings_manager.h"
#include "settings_manager.h"

#include <spdlog/spdlog.h>

namespace helix::settings {

using helix::ui::event_checked;
using helix::ui::event_selected;

void PrintingSettingsOverlay::register_callbacks() {
    register_xml_callbacks({
        {"on_enclosure_style_changed",
         [](lv_event_t* e) {
             SettingsManager::instance().set_enclosure_style(
                 static_cast<helix::bed_drying::EnclosureStyle>(event_selected(e)));
         }},
        {"on_machine_limits_clicked",
         [](lv_event_t*) {
             auto& overlay = get_machine_limits_overlay();
             overlay.set_api(get_moonraker_api());
             overlay.show(get_printing_settings_overlay().parent_screen_);
         }},
        {"on_motion_settings_clicked", [](lv_event_t*) { show_motion_settings_overlay(); }},
        {"on_material_temps_clicked",
         [](lv_event_t*) {
             get_material_temps_overlay().show(get_printing_settings_overlay().parent_screen_);
         }},
        {"on_allow_cold_extrude_changed",
         [](lv_event_t* e) {
             SafetySettingsManager::instance().set_allow_cold_extrude(event_checked(e));
         }},
        {"on_filament_auto_cooldown_changed",
         [](lv_event_t* e) {
             const bool enabled = event_checked(e);
             SettingsManager::instance().set_filament_auto_cooldown(enabled);
             // Turning it off mid-countdown takes effect now, not when the countdown ends.
             if (!enabled) {
                 PostOpCooldownManager::instance().cancel();
             }
         }},
        {"on_filament_auto_open_editor_changed",
         [](lv_event_t* e) {
             SettingsManager::instance().set_filament_auto_open_editor(event_checked(e));
         }},
        {"on_retraction_row_clicked",
         [](lv_event_t*) {
             get_global_retraction_settings().show(get_printing_settings_overlay().parent_screen_);
         }},
        {"on_timelapse_settings_clicked", [](lv_event_t*) { open_timelapse_settings(); }},
        {"on_macro_buttons_clicked",
         [](lv_event_t*) {
             get_macro_buttons_overlay().show(get_printing_settings_overlay().parent_screen_);
         }},
    });
}

} // namespace helix::settings
