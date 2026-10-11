// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_settings_display.cpp
 * @brief Implementation of DisplaySettingsOverlay
 */

#include "ui_settings_display.h"

#include "ui_callback_helpers.h"
#include "ui_panel_settings.h" // get_global_settings_panel() owns the restart prompt
#include "ui_toast_manager.h"

#include "display_manager.h"
#include "display_metrics.h"
#include "display_settings_manager.h"
#include "format_utils.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "ui/ui_widget_helpers.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <string>

namespace helix::settings {

namespace {

/// Explicit UI scale steps offered below Automatic, in dropdown order. 100% is
/// a real choice rather than a synonym for Automatic: it pins a high-DPI panel
/// back to authored sizing. The top of the range is
/// DisplayMetrics::kMaxScaleSettingPercent, so the dropdown cannot ask for a
/// scale the curve itself would refuse.
constexpr int kUiScalePercents[] = {100, 125, 150, 175, 200};
static_assert(kUiScalePercents[0] == helix::DisplayMetrics::kMinScaleSettingPercent);
static_assert(kUiScalePercents[std::size(kUiScalePercents) - 1] ==
              helix::DisplayMetrics::kMaxScaleSettingPercent);

} // namespace

using helix::ui::event_checked;
using helix::ui::event_selected;

void DisplaySettingsOverlay::init_subjects() {
    snprintf(brightness_value_buf_, sizeof(brightness_value_buf_), "100%%");
    UI_MANAGED_SUBJECT_STRING(brightness_value_subject_, brightness_value_buf_,
                              brightness_value_buf_, "brightness_value", subjects_);
}

void DisplaySettingsOverlay::register_callbacks() {
    register_xml_callbacks({
        {"on_display_rotation_changed",
         [](lv_event_t* e) {
             int degrees = DisplaySettingsManager::index_to_rotation_degrees(event_selected(e));
             // The rotation is read once, by DisplayManager at startup, and LVGL screens
             // never re-rotate afterwards. Only prompt when the applied value actually
             // moved - re-picking the current one needs no restart.
             if (DisplaySettingsManager::instance().set_display_rotation(degrees)) {
                 get_global_settings_panel().show_restart_prompt();
             }
         }},
        {"on_brightness_changed",
         [](lv_event_t* e) {
             get_display_settings_overlay().handle_brightness_changed(
                 lv_slider_get_value(lv_event_get_current_target_obj(e)));
         }},
        {"on_brightness_commit",
         [](lv_event_t* e) {
             DisplaySettingsManager::instance().set_brightness(
                 lv_slider_get_value(lv_event_get_current_target_obj(e)));
         }},
        {"on_ui_scale_changed",
         [](lv_event_t* e) {
             const int index = event_selected(e);
             const int percent =
                 (index >= 1 && index <= static_cast<int>(std::size(kUiScalePercents)))
                     ? kUiScalePercents[index - 1]
                     : helix::DisplayMetrics::kScaleSettingAutomatic;
             DisplaySettingsManager::instance().set_ui_scale_percent(percent);

             // Applying live would need every screen to re-tier, and AssetManager only
             // ever registers font tiers upward (register_fonts_for_tier() early-returns
             // on a lower tier), so a downward change cannot un-register the faces it
             // already handed out.
             ToastManager::instance().show(ToastSeverity::INFO,
                                           lv_tr("UI scale applies after restart"));
         }},
        {"on_dim_changed",
         [](lv_event_t* e) {
             DisplaySettingsManager::instance().set_display_dim_sec(
                 DisplaySettingsManager::index_to_dim_seconds(event_selected(e)));
         }},
        {"on_sleep_changed",
         [](lv_event_t* e) {
             DisplaySettingsManager::instance().set_display_sleep_sec(
                 DisplaySettingsManager::index_to_sleep_seconds(event_selected(e)));
         }},
        {"on_sleep_while_printing_changed",
         [](lv_event_t* e) {
             DisplaySettingsManager::instance().set_sleep_while_printing(event_checked(e));
         }},
#ifdef HELIX_ENABLE_SCREENSAVER
        {"on_screensaver_changed",
         [](lv_event_t* e) {
             DisplaySettingsManager::instance().set_screensaver_type(event_selected(e));
         }},
        {"on_test_screensaver",
         [](lv_event_t*) {
             int type = DisplaySettingsManager::instance().get_screensaver_type();
             if (type <= 0) {
                 return; // "Off": the button is hidden then, but guard anyway
             }
             auto* dm = DisplayManager::instance();
             if (!dm) {
                 spdlog::warn("[DisplaySettings] DisplayManager not available, cannot preview "
                              "screensaver");
                 return;
             }
             dm->preview_screensaver(type);
         }},
#else
        {"on_screensaver_changed", [](lv_event_t*) {}},
        {"on_test_screensaver", [](lv_event_t*) {}},
#endif
    });
}

void DisplaySettingsOverlay::on_activate() {
    OverlayBase::on_activate();

    init_display_rotation_dropdown();
    init_brightness_controls();
    init_dim_dropdown();
    init_sleep_dropdown();
    init_ui_scale_dropdown();

#ifndef HELIX_ENABLE_SCREENSAVER
    if (lv_obj_t* ss_row = helix::ui::find_required(overlay_root_, "row_screensaver", get_name())) {
        lv_obj_add_flag(ss_row, LV_OBJ_FLAG_HIDDEN);
    }
#endif
}

// Rows whose widget index is not the stored value (rotation degrees, dim and
// sleep seconds, UI scale percent) are selected here; the toggle and screensaver
// rows bind to their subjects in XML.
void DisplaySettingsOverlay::init_display_rotation_dropdown() {
    lv_obj_t* row = helix::ui::find_required(overlay_root_, "row_display_rotation", get_name());
    if (lv_obj_t* dropdown = helix::ui::find_required(row, "dropdown", get_name())) {
        int degrees = DisplaySettingsManager::instance().get_display_rotation();
        int index = DisplaySettingsManager::rotation_degrees_to_index(degrees);
        lv_dropdown_set_selected(dropdown, static_cast<uint32_t>(index));

        spdlog::debug("[{}] Screen rotation dropdown initialized to index {} ({}°)", get_name(),
                      index, degrees);
    }
}

void DisplaySettingsOverlay::init_brightness_controls() {
    lv_obj_t* brightness_slider =
        helix::ui::find_required(overlay_root_, "brightness_slider", get_name());
    if (brightness_slider) {
        int brightness = DisplaySettingsManager::instance().get_brightness();
        lv_slider_set_value(brightness_slider, brightness, LV_ANIM_OFF);

        helix::format::format_percent(brightness, brightness_value_buf_,
                                      sizeof(brightness_value_buf_));
        lv_subject_copy_string(&brightness_value_subject_, brightness_value_buf_);

        spdlog::debug("[{}] Brightness initialized to {}%", get_name(), brightness);
    }
}

void DisplaySettingsOverlay::init_dim_dropdown() {
    lv_obj_t* dim_row = helix::ui::find_required(overlay_root_, "row_display_dim", get_name());
    if (lv_obj_t* dim_dropdown = helix::ui::find_required(dim_row, "dropdown", get_name())) {
        int current_sec = DisplaySettingsManager::instance().get_display_dim_sec();
        int index = DisplaySettingsManager::dim_seconds_to_index(current_sec);
        lv_dropdown_set_selected(dim_dropdown, index);

        spdlog::debug("[{}] Dim dropdown initialized to index {} ({}s)", get_name(), index,
                      current_sec);
    }
}

void DisplaySettingsOverlay::init_sleep_dropdown() {
    lv_obj_t* sleep_row = helix::ui::find_required(overlay_root_, "row_display_sleep", get_name());
    if (lv_obj_t* sleep_dropdown = helix::ui::find_required(sleep_row, "dropdown", get_name())) {
        int current_sec = DisplaySettingsManager::instance().get_display_sleep_sec();
        int index = DisplaySettingsManager::sleep_seconds_to_index(current_sec);
        lv_dropdown_set_selected(sleep_dropdown, index);

        spdlog::debug("[{}] Sleep dropdown initialized to index {} ({}s)", get_name(), index,
                      current_sec);
    }
}

void DisplaySettingsOverlay::init_ui_scale_dropdown() {
    lv_obj_t* row = helix::ui::find_required(overlay_root_, "row_ui_scale", get_name());
    lv_obj_t* dropdown = helix::ui::find_required(row, "dropdown", get_name());
    if (!dropdown)
        return;

    // The Automatic row carries the percentage the panel's DPI resolved to, so
    // a user on a phone-class screen can see what it chose before deciding to
    // override it. Every shipping printer sits in the DPI deadband and reads
    // "Automatic (100%)".
    const int auto_percent =
        static_cast<int>(std::lround(helix::DisplayMetrics::auto_scale() * 100.0));
    std::string options =
        std::string(lv_tr("Automatic")) + " (" + std::to_string(auto_percent) + "%)";
    for (int percent : kUiScalePercents) {
        options += "\n" + std::to_string(percent) + "%";
    }
    lv_dropdown_set_options(dropdown, options.c_str());

    // Index 0 is Automatic; the explicit steps follow in kUiScalePercents
    // order. A stored value that is not one of the offered steps (hand-edited,
    // or a step we later drop) selects Automatic, which is what it will
    // actually behave as on the next start.
    const int stored = DisplaySettingsManager::instance().get_ui_scale_percent();
    uint32_t selected = 0;
    for (size_t i = 0; i < std::size(kUiScalePercents); ++i) {
        if (kUiScalePercents[i] == stored) {
            selected = static_cast<uint32_t>(i + 1);
            break;
        }
    }
    lv_dropdown_set_selected(dropdown, selected);

    spdlog::debug("[{}] UI scale dropdown initialized: stored={} auto={}%", get_name(), stored,
                  auto_percent);
}

// Per drag tick: apply to the backlight and update the readout, but do NOT
// persist. on_brightness_commit saves once, on release.
void DisplaySettingsOverlay::handle_brightness_changed(int value) {
    DisplaySettingsManager::instance().preview_brightness(value);

    helix::format::format_percent(value, brightness_value_buf_, sizeof(brightness_value_buf_));
    lv_subject_copy_string(&brightness_value_subject_, brightness_value_buf_);
}

} // namespace helix::settings
