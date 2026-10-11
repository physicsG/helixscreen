// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_settings_help.cpp
 * @brief Implementation of HelpSettingsOverlay
 */

#include "ui_settings_help.h"

#include "ui_callback_helpers.h"
#include "ui_debug_bundle_modal.h"
#include "ui_info_qr_modal.h"
#include "ui_nav.h"
#include "ui_next_tick.h"
#include "ui_settings_about.h"

#include "first_run_tour.h"
#include "lvgl/src/others/translation/lv_translation.h"

#include <spdlog/spdlog.h>

namespace helix::settings {

void HelpSettingsOverlay::register_callbacks() {
    register_xml_callbacks({
        {"on_replay_tour_clicked",
         [](lv_event_t*) {
             spdlog::info("[HelpSettingsOverlay] Replay Welcome Tour clicked");
             // Dismiss the Help overlay, then switch to the Home panel so the tour's
             // navbar highlights (and the widget tiles) are actually on screen.
             helix::nav::go_back();
             helix::nav::set_active(helix::PanelId::Home);
             // Defer start so panel activation + layout settle before the overlay
             // resolves target coordinates. Unguarded: the lambda captures no `this`
             // and only touches the immortal FirstRunTour function-local static.
             helix::ui::run_next_tick([]() { helix::tour::FirstRunTour::instance().start(); });
         }},
        {"on_debug_bundle_clicked",
         [](lv_event_t*) {
             spdlog::info("[HelpSettingsOverlay] Upload Debug Bundle clicked");
             DebugBundleModal::show_owned();
         }},
        {"on_discord_clicked",
         [](lv_event_t*) {
             spdlog::info("[HelpSettingsOverlay] Discord clicked");
             helix::ui::InfoQrModal::show_owned({
                 .icon = "message",
                 .title = "Discord Community",
                 .message = lv_tr("Join the HelixScreen community on Discord for discussion, "
                                  "tips, troubleshooting help, and feature requests."),
                 .url = "https://discord.gg/RZCT2StKhr",
                 .url_text = "discord.gg/RZCT2StKhr",
             });
         }},
        {"on_docs_clicked",
         [](lv_event_t*) {
             spdlog::info("[HelpSettingsOverlay] Documentation clicked");
             helix::ui::InfoQrModal::show_owned({
                 .icon = "book",
                 .title = lv_tr("Documentation"),
                 .message = lv_tr("Browse guides, configuration references, and troubleshooting "
                                  "resources for HelixScreen."),
                 .url = "https://helixscreen.org/docs/guide/getting-started/",
                 .url_text = "helixscreen.org/docs",
             });
         }},
        {"on_about_clicked",
         [](lv_event_t*) { get_about_settings_overlay().show(lv_screen_active()); }},
    });
}

} // namespace helix::settings
