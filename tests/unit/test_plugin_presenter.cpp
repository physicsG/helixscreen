// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin surfaces wait for app covers, one plugin's dialog at a time, and a
// plugin never waits on its own dialog.

#if HELIX_HAS_PLUGINS

#include "ui_modal.h"
#include "ui_presentation_gate.h"

#include "../lvgl_ui_test_fixture.h"
#include "display_settings_manager.h"
#include "plugin_presenter.h"

#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::plugin::decide;
using helix::plugin::Decision;
using helix::plugin::PluginPresenter;
using helix::plugin::SurfaceKind;

namespace {

constexpr const char* MODAL = "print_cancel_confirm_modal";

class PresenterFx : public LVGLUITestFixture {
  public:
    std::vector<std::string> events;
    bool wake_locks = false;
    bool locked = false;

    PresenterFx() {
        DisplaySettingsManager::instance().set_animations_enabled(false);
    }
    ~PresenterFx() override {
        helix::ui::UpdateQueue::instance().drain();
        process_lvgl(100);
        DisplaySettingsManager::instance().set_animations_enabled(true);
    }

    void seam(PluginPresenter& p) {
        p.set_covers_source([this] {
            auto c = helix::ui::current_app_covers();
            c.lock_screen = locked;
            return c;
        });
        p.set_wake([this] {
            events.push_back("wake");
            if (wake_locks)
                locked = true;
        });
    }

    /// A present step that opens a dialog tagged with @p plugin, like PluginDialog does.
    PluginPresenter::PresentFn dialog_for(const std::string& plugin, lv_obj_t** out = nullptr) {
        return [this, plugin, out] {
            lv_obj_t* d = Modal::show(MODAL);
            ModalStack::instance().set_tag(ModalStack::instance().backdrop_for(d), plugin);
            if (out)
                *out = d;
            events.push_back("open " + plugin);
            return d != nullptr;
        };
    }
    PluginPresenter::CancelFn cancelled(const std::string& plugin) {
        return [this, plugin] { events.push_back("cancel " + plugin); };
    }
    void close(lv_obj_t* dialog) {
        Modal::hide(dialog);
        helix::ui::UpdateQueue::instance().drain();
        process_lvgl(500);
    }
    bool saw(const std::string& e) const {
        return std::find(events.begin(), events.end(), e) != events.end();
    }
};

} // namespace

TEST_CASE("decide: blocked waits; own dialog presents; other dialog waits; earlier waiter "
          "waits; clear presents",
          "[plugin][presenter]") {
    CHECK(decide(true, "", "a", false) == Decision::Wait);
    CHECK(decide(true, "a", "a", false) == Decision::Wait);
    CHECK(decide(false, "a", "a", true) == Decision::Present);
    CHECK(decide(false, "b", "a", false) == Decision::Wait);
    CHECK(decide(false, "", "a", true) == Decision::Wait);
    CHECK(decide(false, "", "a", false) == Decision::Present);
}

TEST_CASE_METHOD(PresenterFx, "a dialog requested under an app modal presents after it closes",
                 "[plugin][presenter]") {
    PluginPresenter p;
    seam(p);
    lv_obj_t* app = Modal::show(MODAL);
    REQUIRE(app != nullptr);

    const int ticket = p.request("a", SurfaceKind::Dialog, dialog_for("a"), cancelled("a"));
    REQUIRE(ticket > 0);
    CHECK(p.is_queued(ticket));
    CHECK_FALSE(saw("open a"));

    close(app);
    CHECK(saw("open a"));
    CHECK_FALSE(p.is_queued(ticket));
}

TEST_CASE_METHOD(PresenterFx, "two plugins' dialogs present one at a time, FIFO",
                 "[plugin][presenter]") {
    PluginPresenter p;
    seam(p);
    lv_obj_t* first = nullptr;
    p.request("a", SurfaceKind::Dialog, dialog_for("a", &first), cancelled("a"));
    REQUIRE(first != nullptr);
    const int b = p.request("b", SurfaceKind::Dialog, dialog_for("b"), cancelled("b"));
    const int c = p.request("c", SurfaceKind::Overlay, dialog_for("c"), cancelled("c"));
    CHECK(p.is_queued(b));
    CHECK(p.is_queued(c));

    close(first);
    CHECK(saw("open b"));
    CHECK(p.is_queued(c)); // b's dialog now holds c back
}

TEST_CASE_METHOD(PresenterFx, "a plugin's own confirm presents over its own dialog",
                 "[plugin][presenter]") {
    PluginPresenter p;
    seam(p);
    p.request("a", SurfaceKind::Dialog, dialog_for("a"), cancelled("a"));
    p.request("b", SurfaceKind::Dialog, dialog_for("b"), cancelled("b"));
    const int own = p.request("a", SurfaceKind::Confirm, dialog_for("a"), cancelled("a"));
    CHECK_FALSE(p.is_queued(own));
    CHECK(std::count(events.begin(), events.end(), "open a") == 2);
}

TEST_CASE_METHOD(PresenterFx, "withdraw on a queued request runs cancelled, never present",
                 "[plugin][presenter]") {
    PluginPresenter p;
    seam(p);
    lv_obj_t* app = Modal::show(MODAL);
    const int t = p.request("a", SurfaceKind::Dialog, dialog_for("a"), cancelled("a"));
    CHECK(p.withdraw(t));
    CHECK(saw("cancel a"));
    CHECK_FALSE(p.withdraw(t));
    close(app);
    CHECK_FALSE(saw("open a"));
}

TEST_CASE_METHOD(PresenterFx, "drop_plugin runs nothing", "[plugin][presenter]") {
    PluginPresenter p;
    seam(p);
    lv_obj_t* app = Modal::show(MODAL);
    p.request("a", SurfaceKind::Dialog, dialog_for("a"), cancelled("a"));
    CHECK(p.queued_dialogs("a") == 1);
    p.drop_plugin("a");
    CHECK(p.queued_dialogs("a") == 0);
    close(app);
    CHECK(events.empty());
}

TEST_CASE_METHOD(PresenterFx, "presenting wakes the display first", "[plugin][presenter]") {
    PluginPresenter p;
    seam(p);
    p.request("a", SurfaceKind::Dialog, dialog_for("a"), cancelled("a"));
    REQUIRE(events.size() == 2);
    CHECK(events[0] == "wake");
    CHECK(events[1] == "open a");
}

TEST_CASE_METHOD(PresenterFx, "wake that engages auto-lock keeps the request queued",
                 "[plugin][presenter]") {
    PluginPresenter p;
    seam(p);
    wake_locks = true;
    const int t = p.request("a", SurfaceKind::Dialog, dialog_for("a"), cancelled("a"));
    CHECK(p.is_queued(t));
    CHECK_FALSE(saw("open a"));

    wake_locks = false;
    locked = false; // unlocked
    process_lvgl(500);
    CHECK(saw("open a"));
}

TEST_CASE_METHOD(PresenterFx, "a request made while presenting does not re-enter",
                 "[plugin][presenter]") {
    PluginPresenter p;
    seam(p);
    int inner = 0;
    p.request(
        "a", SurfaceKind::Overlay,
        [&] {
            inner = p.request("b", SurfaceKind::Dialog, dialog_for("b"), cancelled("b"));
            events.push_back("open a");
            return true;
        },
        cancelled("a"));
    process_lvgl(500);
    CHECK(inner > 0);
    CHECK(std::count(events.begin(), events.end(), "open a") == 1);
    CHECK(std::count(events.begin(), events.end(), "open b") == 1);
}

#endif // HELIX_HAS_PLUGINS
