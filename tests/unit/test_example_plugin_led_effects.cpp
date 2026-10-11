// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "ui_nav_manager.h"
#include "ui_panel_home.h"

#include "../test_fixtures.h"
#include "../test_helpers/plugin_host_test_support.h"
#include "display_settings_manager.h"
#include "lua_bindings.h"
#include "panel_widget_registry.h"
#include "plugin_host.h"
#include "plugin_xml_policy.h"

#include <fstream>
#include <iterator>

#include "../catch_amalgamated.hpp"

using namespace helix::plugin;
using namespace helix::plugin::test;

namespace {

/// A seeded NavigationManager beside the per-instance printer state, so the fixture
/// matches the conditions the temp-spark cases run under.
class LedFx : public XMLTestFixture {
  public:
    std::array<lv_obj_t*, UI_PANEL_COUNT> panels{};

    LedFx() {
        DisplaySettingsManager::instance().set_animations_enabled(false);
        for (auto& p : panels)
            p = lv_obj_create(lv_screen_active());
        NavigationManager::instance().set_panels(panels.data());
    }
    ~LedFx() override {
        drain();
        process_lvgl(100);
        DisplaySettingsManager::instance().set_animations_enabled(true);
    }
};

/// The enabled block with the gcode permission granted and `effect` chosen.
json led_block(const std::string& effect = "rainbow") {
    json block = enabled("led-effects", {"gcode"});
    block["settings"]["led-effects"] = {{"effect", effect}};
    return block;
}

std::string text_subject(const char* name) {
    return lv_subject_get_string(lv_xml_get_subject(nullptr, name));
}

int int_subject(const char* name) {
    return lv_subject_get_int(lv_xml_get_subject(nullptr, name));
}

/// Answers every pending call request with the queried state of one effect.
void answer_query(HostRig& rig, size_t& next, const std::string& effect, bool on) {
    const json value{{"status", json{{"led_effect " + effect, {{"enabled", on}}}}}};
    while (next < rig.fake.requests.size()) {
        auto& r = rig.fake.requests[next++];
        if (r.kind == "call")
            r.reply(RpcResult{true, value, {}});
    }
    drain();
}

/// Delivers one object delta through the backend's notify_status_update handler.
void deliver_delta(HostRig& rig, const std::string& effect, bool on) {
    const json msg{
        {"params", json::array({json{{"led_effect " + effect, {{"enabled", on}}}}, 0.0})}};
    for (auto& [method, handler] : rig.fake.notify)
        if (method == "notify_status_update")
            handler(msg);
    drain();
}

} // namespace

TEST_CASE_METHOD(LedFx, "led-effects needs the gcode permission", "[plugin][example]") {
    HostRig rig(led_block());
    rig.host->load_from("examples/plugins");
    REQUIRE(rig.info("led-effects"));
    CHECK(rig.info("led-effects")->status == PluginStatus::Loaded);

    const helix::PanelWidgetDef* def = helix::find_widget_def("led-effects__tile");
    REQUIRE(def);
    CHECK(def->category == helix::WidgetCategory::Plugins);
    CHECK(def->colspan == 2); // one cell, in tracks
    CHECK(def->rowspan == 2);
    CHECK(def->max_colspan == 4); // grows to two cells, one row
    CHECK(def->max_rowspan == 2);

    // Loading already ran the policy; one explicit pass keeps that contract named.
    std::ifstream in("examples/plugins/led-effects/ui/led-effects__tile.xml");
    REQUIRE(in);
    std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(check_plugin_xml("led-effects", {"led-effects__tile"}, xml) == std::string());

    // Granted nothing, the manifest's gcode ask parks the plugin before main.lua runs.
    HostRig denied(enabled("led-effects", {}));
    denied.host->load_from("examples/plugins");
    REQUIRE(denied.info("led-effects"));
    CHECK(denied.info("led-effects")->status == PluginStatus::NeedsApproval);
    CHECK(denied.host->runtime("led-effects") == nullptr);
}

TEST_CASE_METHOD(LedFx, "the tile follows the effect's live state", "[plugin][example]") {
    HostRig rig(led_block());
    rig.host->load_from("examples/plugins");

    REQUIRE_FALSE(rig.fake.object_sets.empty());
    CHECK(rig.fake.object_sets.back().first == "led-effects");
    CHECK(rig.fake.object_sets.back().second ==
          json{{"led_effect rainbow", json::array({"enabled"})}});

    size_t next = 0;
    answer_query(rig, next, "rainbow", false);
    CHECK(text_subject("led-effects__state") == "Off");
    CHECK(text_subject("led-effects__effect") == "rainbow");

    deliver_delta(rig, "rainbow", true);
    CHECK(text_subject("led-effects__state") == "On");
    CHECK(int_subject("led-effects__active") == 1);
}

TEST_CASE_METHOD(LedFx, "a tap toggles through helix.gcode", "[plugin][example]") {
    HostRig rig(led_block());
    rig.host->load_from("examples/plugins");
    size_t next = 0;
    answer_query(rig, next, "rainbow", false); // Off

    rig.host->dispatch_event("led-effects__toggle");
    REQUIRE(rig.fake.requests.size() == 2);
    CHECK(rig.fake.requests[1].kind == "gcode");
    CHECK(rig.fake.requests[1].a == "SET_LED_EFFECT EFFECT=rainbow");
    rig.fake.requests[1].reply(RpcResult{true, {}, {}});
    deliver_delta(rig, "rainbow", true);
    CHECK(text_subject("led-effects__state") == "On");

    rig.host->dispatch_event("led-effects__toggle");
    REQUIRE(rig.fake.requests.size() == 3);
    CHECK(rig.fake.requests[2].kind == "gcode");
    CHECK(rig.fake.requests[2].a == "SET_LED_EFFECT EFFECT=rainbow STOP=1");
}

TEST_CASE_METHOD(LedFx, "an unsafe effect name sends nothing", "[plugin][example]") {
    HostRig rig(led_block());
    rig.host->load_from("examples/plugins");
    size_t next = 0;
    answer_query(rig, next, "rainbow", false);

    REQUIRE(rig.host->set_setting("led-effects", "effect", "rainbow\nFIRMWARE_RESTART"));
    CHECK(text_subject("led-effects__effect") == "Invalid name");
    CHECK(rig.fake.object_sets.back().second == json::object()); // old subscription cancelled

    const size_t before = rig.fake.requests.size();
    rig.host->dispatch_event("led-effects__toggle");
    drain();
    CHECK(rig.fake.requests.size() == before); // the name never reaches G-code
    CHECK(text_subject("led-effects__state") == "Off");
}

TEST_CASE_METHOD(LedFx, "an over-long effect name is invalid, not fatal", "[plugin][example]") {
    // subscribe's object-name cap is 64 bytes (kMaxObjectNameBytes,
    // src/plugin/lua_bind_moonraker.cpp); the "led_effect " prefix takes 11.
    constexpr size_t name_cap = 64 - (sizeof("led_effect ") - 1);
    HostRig rig(led_block(std::string(name_cap, 'a')));
    rig.host->load_from("examples/plugins");
    REQUIRE(rig.info("led-effects"));
    CHECK(rig.info("led-effects")->status == PluginStatus::Loaded);
    CHECK(rig.fake.object_sets.back().second ==
          json{{"led_effect " + std::string(name_cap, 'a'), json::array({"enabled"})}});

    // One byte more and the object name would overflow the cap: refused like any
    // invalid name, never raised into a faulted load.
    REQUIRE(rig.host->set_setting("led-effects", "effect", std::string(name_cap + 1, 'a')));
    CHECK(text_subject("led-effects__effect") == "Invalid name");
    CHECK(rig.fake.object_sets.back().second == json::object());
    CHECK(rig.info("led-effects")->status == PluginStatus::Loaded);
}

TEST_CASE_METHOD(LedFx, "switching effects follows only the new one", "[plugin][example]") {
    HostRig rig(led_block());
    rig.host->load_from("examples/plugins");
    size_t next = 0;
    answer_query(rig, next, "rainbow", false);

    REQUIRE(rig.host->set_setting("led-effects", "effect", "breathing"));
    CHECK(rig.fake.object_sets.back().second ==
          json{{"led_effect breathing", json::array({"enabled"})}});

    deliver_delta(rig, "rainbow", true); // a cancelled subscription delivers nothing
    CHECK(text_subject("led-effects__state") == "Off");

    deliver_delta(rig, "breathing", true);
    CHECK(text_subject("led-effects__state") == "On");
}

TEST_CASE_METHOD(LedFx, "a failed toggle keeps Klipper's state", "[plugin][example]") {
    HostRig rig(led_block());
    rig.host->load_from("examples/plugins");
    size_t next = 0;
    answer_query(rig, next, "rainbow", false);

    rig.host->dispatch_event("led-effects__toggle");
    REQUIRE(rig.fake.requests.size() == 2);
    rig.fake.requests[1].reply(RpcResult{false, {}, "Unknown effect"});
    drain();

    CHECK(text_subject("led-effects__state") == "Off");
    CHECK(int_subject("led-effects__active") == 0);
    CHECK(rig.info("led-effects")->status == PluginStatus::Loaded);
}

TEST_CASE_METHOD(LedFx, "the tile names the effect when two cells wide", "[plugin][example]") {
    HostRig rig(led_block());
    rig.host->load_from("examples/plugins");
    size_t next = 0;
    answer_query(rig, next, "rainbow", false);

    LuaRuntime* rt = rig.host->runtime("led-effects");
    REQUIRE(rt);
    // on_size receives cells, not tracks: cols, rows, width, height.
    LuaRuntime::PushFn two_cells = [](lua_State* co) {
        lua_pushinteger(co, 2);
        lua_pushinteger(co, 1);
        lua_pushinteger(co, 200);
        lua_pushinteger(co, 100);
        return 4;
    };
    REQUIRE(dispatch_widget_hook(*rt, "led-effects__tile", WidgetHook::Size, two_cells));
    CHECK(int_subject("led-effects__wide") == 1);

    LuaRuntime::PushFn one_cell = [](lua_State* co) {
        lua_pushinteger(co, 1);
        lua_pushinteger(co, 1);
        lua_pushinteger(co, 100);
        lua_pushinteger(co, 100);
        return 4;
    };
    REQUIRE(dispatch_widget_hook(*rt, "led-effects__tile", WidgetHook::Size, one_cell));
    CHECK(int_subject("led-effects__wide") == 0);
}

TEST_CASE_METHOD(LedFx, "every example plugin loads", "[plugin][example]") {
    HostRig rig(enabled_all({{"temp-spark", {}}, {"led-effects", {"gcode"}}}));
    rig.host->load_from("examples/plugins");
    REQUIRE(rig.info("temp-spark"));
    CHECK(rig.info("temp-spark")->status == PluginStatus::Loaded);
    REQUIRE(rig.info("led-effects"));
    CHECK(rig.info("led-effects")->status == PluginStatus::Loaded);
}

#endif // HELIX_HAS_PLUGINS
