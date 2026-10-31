// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// Modals and overlays are siblings on the active screen, so z-order is child
// order. An overlay pushed while a modal is up must still sit below it.

#include "ui_modal.h"
#include "ui_nav_manager.h"

#include "../lvgl_ui_test_fixture.h"
#include "display_settings_manager.h"

#include "../catch_amalgamated.hpp"

namespace {

class LayeringFx : public LVGLUITestFixture {
  public:
    std::array<lv_obj_t*, kUiPanelCount> panels{};

    LayeringFx() {
        DisplaySettingsManager::instance().set_animations_enabled(false);
        for (auto& p : panels)
            p = lv_obj_create(lv_screen_active());
        NavigationManager::instance().set_panels(panels.data());
    }
    ~LayeringFx() override {
        helix::ui::UpdateQueue::instance().drain();
        process_lvgl(100);
        DisplaySettingsManager::instance().set_animations_enabled(true);
    }

    lv_obj_t* push_overlay() {
        lv_obj_t* overlay = lv_obj_create(lv_screen_active());
        NavigationManager::instance().register_overlay_instance(overlay, nullptr);
        NavigationManager::instance().push_overlay(overlay);
        helix::ui::UpdateQueue::instance().drain();
        process_lvgl(500);
        return overlay;
    }
};

} // namespace

TEST_CASE_METHOD(LayeringFx, "push_overlay keeps a visible modal above the new overlay",
                 "[modal][layering]") {
    lv_obj_t* dialog = helix::ui::modal_alert("Title", "Message");
    REQUIRE(dialog != nullptr);
    lv_obj_t* backdrop = ModalStack::instance().backdrop_for(dialog);
    REQUIRE(backdrop != nullptr);

    lv_obj_t* overlay = push_overlay();
    REQUIRE(lv_obj_get_parent(overlay) == lv_obj_get_parent(backdrop));
    CHECK(lv_obj_get_index(backdrop) > lv_obj_get_index(overlay));
}

TEST_CASE_METHOD(LayeringFx, "push_overlay keeps stacked modals in their order",
                 "[modal][layering]") {
    lv_obj_t* first = helix::ui::modal_alert("First", "Message");
    lv_obj_t* second = helix::ui::modal_alert("Second", "Message");
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    lv_obj_t* lower = ModalStack::instance().backdrop_for(first);
    lv_obj_t* upper = ModalStack::instance().backdrop_for(second);

    lv_obj_t* overlay = push_overlay();
    CHECK(lv_obj_get_index(lower) > lv_obj_get_index(overlay));
    CHECK(lv_obj_get_index(upper) > lv_obj_get_index(lower));
}
