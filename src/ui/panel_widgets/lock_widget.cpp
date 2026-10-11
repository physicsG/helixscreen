// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lock_widget.h"

#include "ui_callback_helpers.h"
#include "ui_event_safety.h"
#include "ui_lock_screen.h"
#include "ui_settings_security.h"

#include "lock_manager.h"
#include "panel_widget_manager.h"
#include "panel_widget_registry.h"

#include <spdlog/spdlog.h>

#include <lvgl.h>

// ============================================================================
// Module-level subject (no PIN state needed in widget — LockManager owns it)
// ============================================================================

// Forward declaration for callback
static void lock_screen_clicked_cb(lv_event_t* e);

namespace helix {

// ============================================================================
// Widget registration
// ============================================================================

void register_lock_widget() {
    // Register XML event callback before any XML is parsed
    lv_xml_register_event_cb(nullptr, "lock_screen_clicked_cb", lock_screen_clicked_cb);
}

} // namespace helix

// ============================================================================
// Static XML event callback
// ============================================================================

static void lock_screen_clicked_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[LockWidget] lock_screen_clicked_cb");
    (void)e;

    if (helix::LockManager::instance().has_pin()) {
        spdlog::info("[LockWidget] Lock button clicked — locking screen");
        helix::LockManager::instance().lock();
        helix::ui::LockScreenOverlay::instance().show();
    } else {
        spdlog::info("[LockWidget] Lock button clicked — no PIN set, opening security settings");
        helix::settings::get_security_settings_overlay().show(lv_screen_active());
    }

    LVGL_SAFE_EVENT_CB_END();
}
