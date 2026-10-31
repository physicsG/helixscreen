// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "../test_fixtures.h"
#include "../test_helpers/log_capture.h"
#include "../test_helpers/plugin_host_test_support.h"
#include "../test_helpers/plugin_test_support.h"
#include "lua_bindings.h"
#include "lua_panel_widget.h"
#include "plugin_host.h"

#include <filesystem>
#include <fstream>

#include "../catch_amalgamated.hpp"

using namespace helix::plugin;
using namespace helix::plugin::test;

TEST_CASE("widget hooks run for a declared widget", "[plugin][lua_bindings_widget]") {
    BoundRuntime b({&install_widget_bindings});
    WidgetDecl d;
    d.id = "test-plugin__tile";
    b.manifest.widgets.push_back(d);
    REQUIRE(b.t.run(R"(
        got = ""
        helix.widget("tile", {
          on_attach = function() got = got .. "a" end,
          on_size = function(c, r, w, h) got = got .. c .. r .. w .. h end,
        }))"));
    CHECK(dispatch_widget_hook(*b.t.rt, "test-plugin__tile", WidgetHook::Attach));
    CHECK(dispatch_widget_hook(*b.t.rt, "test-plugin__tile", WidgetHook::Size, [](lua_State* co) {
        for (int v : {1, 2, 30, 40})
            lua_pushinteger(co, v);
        return 4;
    }));
    CHECK_FALSE(dispatch_widget_hook(*b.t.rt, "test-plugin__tile", WidgetHook::Activate));
    CHECK(b.t.global("got") == "a123040");

    // A second call replaces the handler set: on_attach is gone after
    // re-registering without it, and on_activate arrives with the new set.
    REQUIRE(b.t.run("helix.widget('tile', { on_activate = function() got = got .. 'A' end })"));
    CHECK_FALSE(dispatch_widget_hook(*b.t.rt, "test-plugin__tile", WidgetHook::Attach));
    CHECK(b.t.global("got") == "a123040");
    CHECK(dispatch_widget_hook(*b.t.rt, "test-plugin__tile", WidgetHook::Activate));
    CHECK(b.t.global("got") == "a123040A");
}

TEST_CASE("helix.widget refuses an undeclared widget", "[plugin][lua_bindings_widget]") {
    BoundRuntime b({&install_widget_bindings});
    REQUIRE(b.t.run(R"(ok, err = pcall(helix.widget, "nope", {}))"));
    CHECK(b.t.global("ok") == "false");
    CHECK(b.t.global("err").find("widgets") != std::string::npos);
}

TEST_CASE_METHOD(XMLTestFixture, "a menu check cannot re-register a widget",
                 "[plugin][lua_bindings_widget][check]") {
    BoundRuntime b({&install_ui_bindings, &install_widget_bindings});
    WidgetDecl d;
    d.id = "test-plugin__tile";
    b.manifest.widgets.push_back(d);
    REQUIRE(b.t.run(R"(
        helix.widget("tile", { on_attach = function() end })
        helix.ui.on("check", function() helix.widget("tile", {}); return true end)
    )"));
    helix::LogCapture logs;
    CHECK_FALSE(call_ui_handler_sync(*b.t.rt, "check", {}, {}));
    CHECK(logs.count_containing("helix.widget is not allowed in a menu check") == 1);
    // The handler set registered outside the check is still the live one.
    CHECK(dispatch_widget_hook(*b.t.rt, "test-plugin__tile", WidgetHook::Attach));
    CHECK_FALSE(b.t.rt->faulted());
}

TEST_CASE("helix.widget refuses a hook key holding a non-function",
          "[plugin][lua_bindings_widget]") {
    BoundRuntime b({&install_widget_bindings});
    WidgetDecl d;
    d.id = "test-plugin__tile";
    b.manifest.widgets.push_back(d);
    REQUIRE(b.t.run(R"(ok, err = pcall(helix.widget, "tile", { on_attach = 5 }))"));
    CHECK(b.t.global("ok") == "false");
    CHECK(b.t.global("err").find("on_attach") != std::string::npos);
}

namespace {

/// A plugin `id` declaring widget `<id>__tile`, with `items` as its menu_items body.
void write_tile_plugin(const std::filesystem::path& root, const std::string& id,
                       const std::string& items) {
    std::filesystem::create_directories(root / id / "ui");
    std::ofstream(root / id / "manifest.json") << R"({"id":")" << id << R"(","name":")" << id
                                               << R"(","version":"1.0.0","widgets":[{"id":")" << id
                                               << R"(__tile","name":"Tile","component":")" << id
                                               << R"(__tile"}],"menu_items":[)" << items << "]}";
    std::ofstream(root / id / "ui" / (id + "__tile.xml"))
        << R"(<component><view extends="lv_obj" width="100%" height="100%"/></component>)";
    std::ofstream(root / id / "main.lua")
        << R"(helix.ui.on("go", function(ctx) seen_widget = ctx.widget end))";
}

std::unique_ptr<LuaPanelWidget> tile_widget(PluginHost& host, const std::string& id) {
    return std::make_unique<LuaPanelWidget>(id, id + "__tile", id + "__tile",
                                            host.runtime(id)->token());
}

} // namespace

TEST_CASE_METHOD(XMLTestFixture, "a plugin tile's gear follows its tile menu items",
                 "[plugin][lua_bindings_widget][menu]") {
    TempDir tmp;
    write_tile_plugin(tmp.path, "with-items",
                      R"({"id":"t","menu":"tile","label":"Tile thing","handler":"go"})");
    write_tile_plugin(tmp.path, "no-items",
                      R"({"id":"f","menu":"print_file","label":"File thing","handler":"go"})");
    HostRig rig(enabled_all({{"with-items", {}}, {"no-items", {}}}));
    rig.host->load_from(tmp.path.string());

    CHECK(tile_widget(*rig.host, "with-items")->has_edit_configure());
    CHECK_FALSE(tile_widget(*rig.host, "no-items")->has_edit_configure());

    auto w = tile_widget(*rig.host, "with-items");
    rig.host->disable("with-items");
    CHECK_FALSE(w->has_edit_configure());
}

TEST_CASE_METHOD(XMLTestFixture, "a plugin tile's gear opens only that plugin's tile items",
                 "[plugin][lua_bindings_widget][menu]") {
    REQUIRE(register_component("plugin_items_menu"));
    TempDir tmp;
    write_tile_plugin(tmp.path, "mine",
                      R"({"id":"t","menu":"tile","label":"Mine thing","handler":"go"})");
    write_tile_plugin(tmp.path, "other",
                      R"({"id":"t","menu":"tile","label":"Other thing","handler":"go"})");
    HostRig rig(enabled_all({{"mine", {}}, {"other", {}}}));
    rig.host->load_from(tmp.path.string());

    auto w = tile_widget(*rig.host, "mine");
    lv_obj_t* root = lv_obj_create(test_screen());
    w->attach_tile(root, test_screen());
    CHECK_FALSE(w->on_edit_configure());

    lv_obj_t* rows = lv_obj_find_by_name(test_screen(), "plugin_rows");
    REQUIRE(rows);
    lv_obj_t* label = lv_obj_find_by_name(rows, "item_label");
    REQUIRE(label);
    CHECK(std::string(lv_label_get_text(label)) == "Mine thing");
    uint32_t items = 0;
    for (uint32_t i = 0; i < lv_obj_get_child_count(rows); ++i)
        items += lv_obj_find_by_name(lv_obj_get_child(rows, static_cast<int32_t>(i)),
                                     "item_label") != nullptr;
    CHECK(items == 1);

    lv_obj_send_event(lv_obj_get_parent(lv_obj_get_parent(label)), LV_EVENT_CLICKED, nullptr);
    lua_State* L = rig.host->runtime("mine")->state();
    lua_getglobal(L, "seen_widget");
    CHECK(std::string(lua_tostring(L, -1)) == "tile");
    lua_pop(L, 1);
    w->detach_tile();
    process_lvgl(20);
}

#endif // HELIX_HAS_PLUGINS
