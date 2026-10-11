// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_overlay_rekey_stale_cache.cpp
 * @brief A caller that cached an overlay's root before a hot-reload rebuild can
 *        still reopen it afterwards (prestonbrown/helixscreen#1729).
 *
 * rebuild_active_views() rebuilds closed overlays too, so every root a caller
 * cached before the reload is freed, while the caller keeps registering and
 * pushing it. The reopen must reach the rebuilt root.
 */

#include "ui_nav_manager.h"
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/navigation_manager_test_access.h"
#include "../test_helpers/update_queue_test_access.h"
#include "overlay_base.h"

#include "../catch_amalgamated.hpp"

using helix::PanelId;

namespace {

class CachedOverlay : public OverlayBase {
  public:
    void init_subjects() override {
        subjects_initialized_ = true;
    }

    lv_obj_t* create(lv_obj_t* parent) override {
        parent_screen_ = parent;
        overlay_root_ = lv_obj_create(parent);
        lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);
        return overlay_root_;
    }

    const char* get_name() const override {
        return "CachedOverlay";
    }
};

CachedOverlay* g_lazy_overlay = nullptr;

CachedOverlay& get_lazy_overlay() {
    return *g_lazy_overlay;
}

class StaleCacheFixture : public LVGLUITestFixture {
  protected:
    lv_obj_t* base_ = nullptr;

    StaleCacheFixture() {
        base_ = lv_obj_create(test_screen());
        lv_obj_t* panels[UI_PANEL_COUNT] = {nullptr};
        panels[static_cast<int>(PanelId::Home)] = base_;
        NavigationManager::instance().set_panels(panels);
    }

    ~StaleCacheFixture() override {
        lv_obj_delete(base_);
    }

    static void settle() {
        for (int i = 0; i < 5; ++i) {
            helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
            lv_timer_handler();
        }
    }

    /// Close the overlay, then rebuild everything as a hot reload does, and
    /// return the root the caller cached before it.
    lv_obj_t* close_and_rebuild(OverlayBase& overlay) {
        auto& nav = NavigationManager::instance();
        lv_obj_t* cached = overlay.get_root();
        REQUIRE(nav.is_panel_on_top(cached));
        nav.go_back();
        settle();
        REQUIRE_FALSE(nav.is_panel_in_stack(cached));

        nav.rebuild_active_views();
        settle();
        // The precondition the reopen is about: the cached root is gone.
        REQUIRE(overlay.get_root() != nullptr);
        REQUIRE(overlay.get_root() != cached);
        REQUIRE_FALSE(lv_obj_is_valid(cached));
        return cached;
    }
};

} // namespace

TEST_CASE_METHOD(StaleCacheFixture, "A root cached by its caller reopens after a rebuild",
                 "[1729][navigation][overlay][hot_reload]") {
    auto& nav = NavigationManager::instance();
    CachedOverlay overlay;
    overlay.init_subjects();

    // The hand-rolled cache many overlays keep: create once, then register and
    // push the cached root on every open.
    lv_obj_t* cached = overlay.create(test_screen());
    nav.register_overlay_instance(cached, &overlay);
    nav.push_overlay(cached);
    settle();

    lv_obj_t* stale = close_and_rebuild(overlay);
    REQUIRE(stale == cached);

    nav.register_overlay_instance(cached, &overlay);
    nav.push_overlay(cached);
    settle();

    CHECK(nav.is_panel_on_top(overlay.get_root()));
    CHECK_FALSE(lv_obj_has_flag(overlay.get_root(), LV_OBJ_FLAG_HIDDEN));

    nav.go_back();
    settle();
}

TEST_CASE_METHOD(StaleCacheFixture,
                 "A root cached by the lazy overlay helper reopens after a rebuild",
                 "[1729][navigation][overlay][hot_reload]") {
    auto& nav = NavigationManager::instance();
    CachedOverlay overlay;
    g_lazy_overlay = &overlay;
    lv_obj_t* cached = nullptr;

    REQUIRE(get_lazy_overlay().show(test_screen()));
    cached = get_lazy_overlay().get_root();
    settle();

    close_and_rebuild(overlay);

    REQUIRE(get_lazy_overlay().show(test_screen()));
    cached = get_lazy_overlay().get_root();
    settle();

    CHECK(nav.is_panel_on_top(overlay.get_root()));

    nav.go_back();
    settle();
    g_lazy_overlay = nullptr;
}

TEST_CASE_METHOD(StaleCacheFixture, "A push queued before a rebuild opens the rebuilt root",
                 "[1729][navigation][overlay][hot_reload]") {
    auto& nav = NavigationManager::instance();
    CachedOverlay overlay;
    overlay.init_subjects();
    lv_obj_t* cached = overlay.create(test_screen());
    nav.register_overlay_instance(cached, &overlay);

    // The push is queued, and the rebuild runs before it drains.
    nav.push_overlay(cached);
    nav.rebuild_active_views();
    // The replaced root awaits its deferred delete: still a valid object.
    REQUIRE(overlay.get_root() != cached);
    REQUIRE(lv_obj_is_valid(cached));
    settle();

    CHECK(nav.is_panel_on_top(overlay.get_root()));

    nav.go_back();
    settle();
}

TEST_CASE_METHOD(StaleCacheFixture,
                 "A later object at a replaced root's address is never forwarded to its successor",
                 "[1729][navigation][overlay][hot_reload]") {
    auto& nav = NavigationManager::instance();
    CachedOverlay overlay;
    overlay.init_subjects();
    lv_obj_t* cached = overlay.create(test_screen());
    nav.register_overlay_instance(cached, &overlay);
    nav.push_overlay(cached);
    settle();
    lv_obj_t* stale = close_and_rebuild(overlay);

    // Another overlay's root at the freed address. The navigation manager keys
    // forwarding by address alone, so the entry moves onto a fresh object
    // instead of waiting for the allocator to reuse the address, which an
    // allocator with a free-list quarantine (ASAN) never does in a test's span.
    lv_obj_t* tenant = lv_obj_create(test_screen());
    lv_obj_add_flag(tenant, LV_OBJ_FLAG_HIDDEN);
    REQUIRE(NavigationManagerTestAccess::readdress_rebuilt_overlay(nav, stale, tenant));

    nav.register_overlay_instance(tenant, nullptr);
    nav.push_overlay(tenant);
    settle();
    REQUIRE(nav.is_panel_on_top(tenant));
    nav.go_back();
    settle();

    // The tenant is freed too, and its cache pushed: nothing opens, least of
    // all the rebuilt overlay that once lived at this address.
    lv_obj_delete(tenant);
    settle();
    nav.push_overlay(tenant);
    settle();

    CHECK_FALSE(nav.is_panel_on_top(overlay.get_root()));
    CHECK_FALSE(nav.is_panel_in_stack(overlay.get_root()));
}
