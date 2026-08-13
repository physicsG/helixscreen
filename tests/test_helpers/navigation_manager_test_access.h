// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_nav_manager.h"

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

    /// Drive the navbar path directly. The production entry point is a static
    /// LV_EVENT_CLICKED handler on the navbar button, which a test cannot reach
    /// without a real navbar; this is the body that handler queues.
    static void switch_to_panel(NavigationManager& nav, helix::PanelId panel_id) {
        nav.switch_to_panel_impl(static_cast<int>(panel_id));
    }
};
