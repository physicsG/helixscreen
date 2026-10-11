// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_nav_forwards.cpp
 * @brief helix::nav free functions reach the NavigationManager singleton
 *
 * ui_nav.h is the narrow header most callers include; each forward has to land
 * on the same manager call the full class exposes.
 */

#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/navigation_manager_test_access.h"
#include "../test_helpers/update_queue_test_access.h"
#include "panel_lifecycle.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

class NullLifecycle : public IPanelLifecycle {
  public:
    void on_activate() override {}
    void on_deactivate(DeactivateReason) override {}
    const char* get_name() const override {
        return "NullLifecycle";
    }
};

class NavForwardsFixture : public LVGLUITestFixture {
  public:
    NavForwardsFixture() {
        home_ = lv_obj_create(test_screen());
        controls_ = lv_obj_create(test_screen());
        lv_obj_t* panels[UI_PANEL_COUNT] = {nullptr};
        panels[static_cast<int>(PanelId::Home)] = home_;
        panels[static_cast<int>(PanelId::Controls)] = controls_;
        NavigationManager::instance().set_panels(panels);

        overlay_ = lv_obj_create(test_screen());
        lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    }

    ~NavForwardsFixture() override {
        nav::unregister_overlay(overlay_);
        drain();
    }

    static void drain() {
        ui::UpdateQueueTestAccess::drain_all(ui::UpdateQueue::instance());
    }

    lv_obj_t* home_ = nullptr;
    lv_obj_t* controls_ = nullptr;
    lv_obj_t* overlay_ = nullptr;
    NullLifecycle lifecycle_;
};

} // namespace

TEST_CASE_METHOD(NavForwardsFixture, "nav::push_overlay and go_back drive the overlay stack",
                 "[navigation][nav_forwards]") {
    auto& mgr = NavigationManager::instance();
    nav::register_overlay(overlay_, &lifecycle_);

    nav::push_overlay(overlay_);
    drain();
    CHECK(nav::is_in_stack(overlay_));
    CHECK(nav::is_on_top(overlay_));
    CHECK(mgr.has_open_overlays());
    CHECK_FALSE(nav::is_on_top(home_));
    CHECK(nav::is_in_stack(home_));

    CHECK(nav::go_back());
    drain();
    CHECK_FALSE(nav::is_in_stack(overlay_));
    CHECK_FALSE(mgr.has_open_overlays());
}

TEST_CASE_METHOD(NavForwardsFixture, "nav::close_overlay pops the overlay it names",
                 "[navigation][nav_forwards]") {
    nav::register_overlay(overlay_, &lifecycle_);
    nav::push_overlay(overlay_);
    drain();
    REQUIRE(nav::is_on_top(overlay_));

    nav::close_overlay(overlay_);
    drain();
    CHECK_FALSE(nav::is_in_stack(overlay_));
}

TEST_CASE_METHOD(NavForwardsFixture,
                 "nav::register_overlay and unregister_overlay pair a lifecycle",
                 "[navigation][nav_forwards]") {
    auto& mgr = NavigationManager::instance();

    nav::register_overlay(overlay_, &lifecycle_);
    CHECK(NavigationManagerTestAccess::lifecycle_of(mgr, overlay_) == &lifecycle_);

    nav::unregister_overlay(overlay_);
    CHECK(NavigationManagerTestAccess::lifecycle_of(mgr, overlay_) == nullptr);
}

TEST_CASE_METHOD(NavForwardsFixture, "nav::on_close and clear_on_close manage the close callback",
                 "[navigation][nav_forwards]") {
    auto& mgr = NavigationManager::instance();

    nav::on_close(overlay_, [] {});
    CHECK(mgr.has_overlay_close_callback(overlay_));

    nav::clear_on_close(overlay_);
    CHECK_FALSE(mgr.has_overlay_close_callback(overlay_));
}

TEST_CASE_METHOD(NavForwardsFixture, "nav::set_active swaps the base panel",
                 "[navigation][nav_forwards]") {
    nav::set_active(PanelId::Controls);
    drain();
    CHECK(NavigationManager::instance().get_active() == PanelId::Controls);

    nav::set_active(PanelId::Home);
    drain();
    CHECK(NavigationManager::instance().get_active() == PanelId::Home);
}

TEST_CASE_METHOD(NavForwardsFixture,
                 "nav::set_overlay_width_unmanaged exempts an overlay from push-time width",
                 "[navigation][nav_forwards]") {
    constexpr int32_t kCustomWidth = 123;
    lv_obj_t* managed = lv_obj_create(test_screen());
    lv_obj_add_flag(managed, LV_OBJ_FLAG_HIDDEN);
    for (lv_obj_t* o : {overlay_, managed}) {
        lv_obj_set_style_width(o, kCustomWidth, LV_PART_MAIN);
        nav::register_overlay(o, &lifecycle_);
    }
    nav::set_overlay_width_unmanaged(overlay_);

    nav::push_overlay(managed);
    drain();
    nav::go_back();
    drain();
    nav::push_overlay(overlay_);
    drain();

    CHECK(lv_obj_get_style_width(overlay_, LV_PART_MAIN) == kCustomWidth);
    CHECK(lv_obj_get_style_width(managed, LV_PART_MAIN) != kCustomWidth);

    nav::unregister_overlay(managed);
}
