// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_nav_manager.h"

#include <utility>
#include <vector>

class NavigationManagerTestAccess {
  public:
    /// Force the keyboard-visible-at-press latch that take_backdrop_keyboard_dismiss()
    /// consults. The latch is normally set at LV_EVENT_PRESSED from live keyboard
    /// state; setting it directly keeps dismiss tests deterministic.
    static void set_backdrop_press_keyboard_visible(NavigationManager& nav, bool visible) {
        nav.backdrop_press_keyboard_visible_ = visible;
    }

    /// Read the raw backdrop pointer, to assert it does not outlive the widget.
    static lv_obj_t* overlay_backdrop(NavigationManager& nav) {
        return nav.overlay_backdrop_;
    }

    /// Create the darkened snapshot backdrop, as the first push_overlay() does.
    static void adopt_overlay_backdrop(NavigationManager& nav, lv_obj_t* screen) {
        nav.adopt_overlay_backdrop(screen);
    }

    /// Re-take the backdrop snapshot from the live tree.
    static void refresh_overlay_backdrop(NavigationManager& nav) {
        nav.refresh_overlay_backdrop();
    }

    /// Seed panel_stack_ without going through the queued push path, so a test
    /// can put a stand-in base panel and overlay in the stack synchronously.
    static void set_panel_stack(NavigationManager& nav, std::vector<lv_obj_t*> stack) {
        nav.panel_stack_ = std::move(stack);
    }

    /// Drive the navbar path directly. The production entry point is a static
    /// LV_EVENT_CLICKED handler on the navbar button, which a test cannot reach
    /// without a real navbar; this is the body that handler queues.
    static void switch_to_panel(NavigationManager& nav, helix::PanelId panel_id) {
        nav.switch_to_panel_impl(static_cast<int>(panel_id));
    }
};
