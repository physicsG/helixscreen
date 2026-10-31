// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// A plugin dialog is a real modal stack entry that tells its plugin, once,
// how it ended.

#if HELIX_HAS_PLUGINS

#include "ui_modal.h"

#include "../lvgl_ui_test_fixture.h"
#include "display_settings_manager.h"
#include "plugin_dialog.h"

#include <optional>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::plugin::close_how_for;
using helix::plugin::close_how_name;
using helix::plugin::CloseHow;
using helix::plugin::PluginDialog;

namespace {

constexpr const char* COMPONENT = "test-plugin__dialog";

class DialogFx : public LVGLUITestFixture {
  public:
    std::vector<CloseHow> closes;

    DialogFx() {
        DisplaySettingsManager::instance().set_animations_enabled(false);
        lv_xml_register_component_from_data(
            COMPONENT, "<component><view extends=\"lv_obj\" width=\"200\" height=\"100\"/>"
                       "</component>");
    }
    ~DialogFx() override {
        helix::ui::UpdateQueue::instance().drain();
        process_lvgl(100);
        DisplaySettingsManager::instance().set_animations_enabled(true);
    }

    lv_obj_t* open() {
        auto d = std::make_unique<PluginDialog>("test-plugin", COMPONENT,
                                                std::vector<std::pair<std::string, std::string>>{},
                                                [this](CloseHow how) { closes.push_back(how); });
        return PluginDialog::present(std::move(d));
    }
    void settle() {
        helix::ui::UpdateQueue::instance().drain();
        process_lvgl(500);
    }
};

} // namespace

TEST_CASE("close_how_for maps every ModalCloseReason", "[plugin][dialog]") {
    CHECK(close_how_for(ModalCloseReason::BackdropTap) == CloseHow::User);
    CHECK(close_how_for(ModalCloseReason::EscKey) == CloseHow::User);
    CHECK(close_how_for(ModalCloseReason::ButtonPress) == CloseHow::User);
    CHECK(close_how_for(ModalCloseReason::Programmatic) == CloseHow::Plugin);
    CHECK(close_how_for(ModalCloseReason::External) == CloseHow::App);
    CHECK(close_how_for(ModalCloseReason::HotReload) == CloseHow::App);
}

TEST_CASE("close_how_name spells the four values", "[plugin][dialog]") {
    CHECK(std::string(close_how_name(CloseHow::User)) == "user");
    CHECK(std::string(close_how_name(CloseHow::Plugin)) == "plugin");
    CHECK(std::string(close_how_name(CloseHow::App)) == "app");
    CHECK(std::string(close_how_name(CloseHow::Cancelled)) == "cancelled");
}

TEST_CASE_METHOD(DialogFx, "present tags the entry with the plugin id", "[plugin][dialog]") {
    lv_obj_t* dialog = open();
    REQUIRE(dialog != nullptr);
    CHECK(ModalStack::instance().top_visible_tag() == "test-plugin");
    CHECK_FALSE(ModalStack::instance().has_visible_untagged());
}

TEST_CASE_METHOD(DialogFx, "backdrop tap reports user", "[plugin][dialog]") {
    lv_obj_t* dialog = open();
    REQUIRE(dialog != nullptr);
    lv_obj_send_event(ModalStack::instance().backdrop_for(dialog), LV_EVENT_CLICKED, nullptr);
    settle();
    REQUIRE(closes.size() == 1);
    CHECK(closes[0] == CloseHow::User);
}

TEST_CASE_METHOD(DialogFx, "close_by_plugin reports plugin", "[plugin][dialog]") {
    lv_obj_t* dialog = open();
    REQUIRE(PluginDialog::close_by_plugin(dialog));
    settle();
    REQUIRE(closes.size() == 1);
    CHECK(closes[0] == CloseHow::Plugin);
}

TEST_CASE_METHOD(DialogFx, "an External hide reports app", "[plugin][dialog]") {
    lv_obj_t* dialog = open();
    Modal::hide(dialog, ModalCloseReason::External);
    settle();
    REQUIRE(closes.size() == 1);
    CHECK(closes[0] == CloseHow::App);
}

TEST_CASE_METHOD(DialogFx, "forget then hide runs nothing", "[plugin][dialog]") {
    lv_obj_t* dialog = open();
    PluginDialog::forget(dialog);
    REQUIRE(PluginDialog::close_by_plugin(dialog));
    settle();
    CHECK(closes.empty());
}

TEST_CASE_METHOD(DialogFx, "on_closed runs exactly once", "[plugin][dialog]") {
    lv_obj_t* dialog = open();
    REQUIRE(PluginDialog::close_by_plugin(dialog));
    PluginDialog::close_by_plugin(dialog);
    Modal::hide(dialog, ModalCloseReason::External);
    settle();
    CHECK(closes.size() == 1);
}

TEST_CASE_METHOD(DialogFx, "a closed dialog is no longer found", "[plugin][dialog]") {
    lv_obj_t* dialog = open();
    REQUIRE(PluginDialog::close_by_plugin(dialog));
    settle();
    CHECK_FALSE(PluginDialog::close_by_plugin(dialog));
}

#endif // HELIX_HAS_PLUGINS
