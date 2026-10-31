// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "../lvgl_test_fixture.h"
#include "../test_helpers/log_capture.h"
#include "../test_helpers/plugin_host_test_support.h"
#include "helix-xml/src/xml/lv_xml_translation.h"
#include "plugin_host.h"
#include "plugin_menu.h"
#include "scope_exit.h"

#include <filesystem>
#include <fstream>

#include "../catch_amalgamated.hpp"

using namespace helix::plugin;
using namespace helix::plugin::test;
namespace pm = helix::plugin_menu;

namespace {

/// A loadable plugin named `id` with the given menu_items JSON array body and main.lua.
void write_menu_plugin(const std::filesystem::path& root, const std::string& id,
                       const std::string& name, const std::string& items, const std::string& lua,
                       const std::string& extra = "") {
    std::filesystem::create_directories(root / id);
    std::ofstream(root / id / "manifest.json")
        << R"({"id":")" << id << R"(","name":")" << name << R"(","version":"1.0.0",)" << extra
        << R"("menu_items":[)" << items << "]}";
    std::ofstream(root / id / "main.lua") << lua;
}

std::string spool_item(const std::string& id, const std::string& extra = "") {
    return R"({"id":")" + id + R"(","menu":"spool","label":"Item )" + id + R"(","handler":"run")" +
           extra + "}";
}

std::vector<std::string> item_ids(const std::vector<pm::Entry>& entries) {
    std::vector<std::string> out;
    for (const auto& e : entries)
        out.push_back(e.plugin_id + "/" + e.item_id);
    return out;
}

constexpr const char* kRunLua = R"(helix.ui.on("run", function(ctx) end))";

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "declared items resolve with labels in load order",
                 "[plugin][menu]") {
    TempDir tmp;
    write_menu_plugin(tmp.path, "b-plug", "Bee", spool_item("x"), kRunLua);
    write_menu_plugin(tmp.path, "a-plug", "Ay",
                      spool_item("one", R"(,"icon":"tune")") + "," + spool_item("two"), kRunLua);
    HostRig rig(enabled_all({{"a-plug", {}}, {"b-plug", {}}}));
    rig.host->load_from(tmp.path.string());

    auto entries = pm::resolve(pm::MenuKind::Spool, json::object());
    CHECK(item_ids(entries) == std::vector<std::string>{"a-plug/one", "a-plug/two", "b-plug/x"});
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].plugin_name == "Ay");
    CHECK(entries[0].label == "Item one");
    CHECK(entries[0].icon == "tune");
    CHECK(entries[0].enabled);
    CHECK(entries[0].reason.empty());
    CHECK(entries[0].gen != 0);
    CHECK(entries[2].plugin_name == "Bee");
    CHECK(entries[1].icon.empty());

    CHECK(pm::resolve(pm::MenuKind::PrintFile, json::object()).empty());
    CHECK(item_ids(pm::resolve(pm::MenuKind::Spool, json::object(), "b-plug")) ==
          std::vector<std::string>{"b-plug/x"});
}

TEST_CASE_METHOD(LVGLTestFixture, "a menu item's plugin heading is translated like its label",
                 "[plugin][menu][i18n]") {
    helix::ScopeExit restore([] { lv_translation_set_language("en"); });
    TempDir tmp;
    write_menu_plugin(tmp.path, "tr-plug", "Heading Name", spool_item("one"), kRunLua);
    std::filesystem::create_directories(tmp.path / "tr-plug" / "i18n");
    std::ofstream(tmp.path / "tr-plug" / "i18n" / "de.xml")
        << "<translations languages=\"de\">"
           "<translation tag=\"Heading Name\" de=\"Kopfname\"/>"
           "<translation tag=\"Item one\" de=\"Eintrag eins\"/>"
           "</translations>";
    HostRig rig(enabled_all({{"tr-plug", {}}}));
    rig.host->load_from(tmp.path.string());
    lv_translation_set_language("de");

    auto entries = pm::resolve(pm::MenuKind::Spool, json::object());
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].label == "Eintrag eins");
    CHECK(entries[0].plugin_name == "Kopfname");
}

TEST_CASE_METHOD(LVGLTestFixture, "any_declared answers from declarations", "[plugin][menu]") {
    TempDir tmp;
    write_menu_plugin(tmp.path, "a-plug", "Ay", spool_item("x"),
                      R"(helix.ui.on("run", function() end)
                         helix.ui.on("never", function() return false end))");
    HostRig rig(enabled("a-plug", {}));
    CHECK_FALSE(pm::any_declared(pm::MenuKind::Spool));
    rig.host->load_from(tmp.path.string());
    CHECK(pm::any_declared(pm::MenuKind::Spool));
    CHECK(pm::any_declared(pm::MenuKind::Spool, "a-plug"));
    CHECK_FALSE(pm::any_declared(pm::MenuKind::Spool, "b-plug"));
    CHECK_FALSE(pm::any_declared(pm::MenuKind::PrintFile));
    CHECK_FALSE(pm::any_declared(pm::MenuKind::Tile));
}

TEST_CASE_METHOD(LVGLTestFixture, "check false hides; table disables with reason",
                 "[plugin][menu]") {
    TempDir tmp;
    write_menu_plugin(tmp.path, "a-plug", "Ay",
                      spool_item("hidden", R"(,"check":"no")") + "," +
                          spool_item("grey", R"(,"check":"grey")") + "," +
                          spool_item("shown", R"(,"check":"yes")") + "," +
                          spool_item("ctx", R"(,"check":"by-ctx")"),
                      R"(
        helix.ui.on("run", function() end)
        helix.ui.on("no", function() return false end)
        helix.ui.on("yes", function() return true end)
        helix.ui.on("grey", function()
            return {visible = true, enabled = false, reason = "Not now"}
        end)
        helix.ui.on("by-ctx", function(ctx) return ctx.spool_id == 42 end)
    )");
    HostRig rig(enabled("a-plug", {}));
    rig.host->load_from(tmp.path.string());

    auto entries = pm::resolve(pm::MenuKind::Spool, json{{"spool_id", 42}});
    CHECK(item_ids(entries) ==
          std::vector<std::string>{"a-plug/grey", "a-plug/shown", "a-plug/ctx"});
    REQUIRE(entries.size() == 3);
    CHECK_FALSE(entries[0].enabled);
    CHECK(entries[0].reason == "Not now");
    CHECK(entries[1].enabled);
    CHECK(item_ids(pm::resolve(pm::MenuKind::Spool, json{{"spool_id", 7}})) ==
          std::vector<std::string>{"a-plug/grey", "a-plug/shown"});
    CHECK_FALSE(rig.host->runtime("a-plug")->faulted());
}

TEST_CASE_METHOD(LVGLTestFixture, "failing check hides only that item, plugin stays loaded",
                 "[plugin][menu]") {
    TempDir tmp;
    write_menu_plugin(tmp.path, "a-plug", "Ay",
                      spool_item("raises", R"(,"check":"raises")") + "," +
                          spool_item("bad", R"(,"check":"bad")") + "," + spool_item("fine"),
                      R"(
        helix.ui.on("run", function() end)
        helix.ui.on("raises", function() error("boom") end)
        helix.ui.on("bad", function() return 1 end)
    )");
    HostRig rig(enabled("a-plug", {}));
    rig.host->load_from(tmp.path.string());
    helix::LogCapture logs;
    CHECK(item_ids(pm::resolve(pm::MenuKind::Spool, json::object())) ==
          std::vector<std::string>{"a-plug/fine"});
    CHECK(logs.count_containing("boom") == 1);
    CHECK(logs.count_containing("menu check 'bad' must return a boolean") == 1);
    drain();
    REQUIRE(rig.info("a-plug"));
    CHECK(rig.info("a-plug")->status == PluginStatus::Loaded);
}

TEST_CASE_METHOD(LVGLTestFixture, "disabled plugin contributes nothing", "[plugin][menu]") {
    TempDir tmp;
    write_menu_plugin(tmp.path, "a-plug", "Ay", spool_item("x"), kRunLua);
    HostRig rig(enabled("a-plug", {}));
    rig.host->load_from(tmp.path.string());
    REQUIRE(pm::resolve(pm::MenuKind::Spool, json::object()).size() == 1);
    rig.host->disable("a-plug");
    CHECK(pm::resolve(pm::MenuKind::Spool, json::object()).empty());
    CHECK_FALSE(pm::any_declared(pm::MenuKind::Spool));
}

TEST_CASE_METHOD(LVGLTestFixture, "faulted runtime contributes nothing", "[plugin][menu]") {
    TempDir tmp;
    write_menu_plugin(tmp.path, "a-plug", "Ay", spool_item("x"), R"(
        runs = 0
        helix.ui.on("run", function() runs = runs + 1 end)
        helix.ui.on("spin", function() while true do end end)
    )");
    HostRig rig(enabled("a-plug", {}));
    rig.host->load_from(tmp.path.string());
    auto before = pm::resolve(pm::MenuKind::Spool, json::object());
    REQUIRE(before.size() == 1);
    // Faulted but not yet unloaded: the unload is deferred to the main loop.
    dispatch_ui_handler(*rig.host->runtime("a-plug"), "spin", std::nullopt);
    LuaRuntime* rt = rig.host->runtime("a-plug");
    REQUIRE(rt);
    REQUIRE(rt->faulted());
    CHECK(pm::resolve(pm::MenuKind::Spool, json::object()).empty());
    CHECK_FALSE(pm::any_declared(pm::MenuKind::Spool));
    pm::activate(before[0], json::object());
    lua_State* L = rt->state();
    lua_getglobal(L, "runs");
    CHECK(lua_tointeger(L, -1) == 0);
    lua_pop(L, 1);
    drain();
    CHECK(rig.host->runtime("a-plug") == nullptr);
    pm::activate(before[0], json::object());
}

TEST_CASE_METHOD(LVGLTestFixture, "activate runs the handler with the context", "[plugin][menu]") {
    TempDir tmp;
    write_menu_plugin(tmp.path, "a-plug", "Ay", spool_item("x"), R"(
        helix.ui.on("run", function(ctx) seen_spool = ctx.spool_id; seen_missing = ctx.missing end)
    )");
    HostRig rig(enabled("a-plug", {}));
    rig.host->load_from(tmp.path.string());
    json ctx{{"spool_id", 42}};
    auto entries = pm::resolve(pm::MenuKind::Spool, ctx);
    REQUIRE(entries.size() == 1);
    pm::activate(entries[0], ctx);
    lua_State* L = rig.host->runtime("a-plug")->state();
    lua_getglobal(L, "seen_spool");
    CHECK(lua_tointeger(L, -1) == 42);
    lua_getglobal(L, "seen_missing");
    CHECK(lua_isnil(L, -1));
    lua_pop(L, 2);
}

TEST_CASE_METHOD(LVGLTestFixture, "activate after unload does nothing", "[plugin][menu]") {
    TempDir tmp;
    write_menu_plugin(tmp.path, "a-plug", "Ay", spool_item("x"), R"(
        runs = 0
        helix.ui.on("run", function() runs = runs + 1 end)
    )");
    HostRig rig(enabled("a-plug", {}));
    rig.host->load_from(tmp.path.string());
    auto entries = pm::resolve(pm::MenuKind::Spool, json::object());
    REQUIRE(entries.size() == 1);
    rig.host->disable("a-plug");
    pm::activate(entries[0], json::object()); // nothing loaded: no-op
    REQUIRE(rig.host->enable("a-plug"));
    pm::activate(entries[0], json::object()); // a newer load of the same id
    lua_State* L = rig.host->runtime("a-plug")->state();
    lua_getglobal(L, "runs");
    CHECK(lua_tointeger(L, -1) == 0);
    lua_pop(L, 1);
    auto fresh = pm::resolve(pm::MenuKind::Spool, json::object());
    REQUIRE(fresh.size() == 1);
    pm::activate(fresh[0], json::object());
    lua_getglobal(L, "runs");
    CHECK(lua_tointeger(L, -1) == 1);
    lua_pop(L, 1);
}

TEST_CASE_METHOD(LVGLTestFixture, "cap applies across plugins", "[plugin][menu]") {
    REQUIRE(pm::row_cap(pm::MenuKind::Spool) == 6);
    TempDir tmp;
    std::string three = spool_item("i1") + "," + spool_item("i2") + "," + spool_item("i3");
    for (const char* id : {"a-plug", "b-plug", "c-plug"})
        write_menu_plugin(tmp.path, id, id, three, kRunLua);
    HostRig rig(enabled_all({{"a-plug", {}}, {"b-plug", {}}, {"c-plug", {}}}));
    rig.host->load_from(tmp.path.string());
    CHECK(item_ids(pm::resolve(pm::MenuKind::Spool, json::object())) ==
          std::vector<std::string>{"a-plug/i1", "a-plug/i2", "a-plug/i3", "b-plug/i1", "b-plug/i2",
                                   "b-plug/i3"});
}

TEST_CASE("menu calls without a host are empty", "[plugin][menu]") {
    REQUIRE(PluginHost::live() == nullptr);
    CHECK_FALSE(pm::any_declared(pm::MenuKind::Spool));
    CHECK(pm::resolve(pm::MenuKind::Spool, json::object()).empty());
    pm::activate(pm::Entry{"a-plug", "Ay", "x", "X", "", "", true, 1}, json::object());
    CHECK(pm::row_cap(pm::MenuKind::PrintFile) == 8);
    CHECK(pm::row_cap(pm::MenuKind::Tile) == 4);
}

TEST_CASE_METHOD(LVGLTestFixture, "a check result with a metatable is refused unread",
                 "[plugin][menu]") {
    struct Case {
        const char* id;
        const char* index_fn;
    };
    // Each __index would misbehave if anything read the table through it.
    const Case cases[] = {
        {"m-error", R"(function() error("from __index") end)"},
        {"m-loop", R"(function() while true do end end)"},
        {"m-store", R"(function() helix.storage.set("leak", 1); return true end)"},
    };
    TempDir tmp;
    json block = json{{"enabled", json::object()}};
    for (const auto& c : cases) {
        write_menu_plugin(tmp.path, c.id, c.id,
                          spool_item("x", R"(,"check":"meta")"), std::string(R"(
            helix.ui.on("run", function() end)
            helix.ui.on("meta", function()
                return setmetatable({}, {__index = )") + c.index_fn + R"(})
            end)
            helix.ui.on("probe", function() probe = helix.storage.get("leak") end)
        )",
                          R"("permissions":["storage"],)");
        block["enabled"][c.id] = {{"version", "1.0.0"}, {"permissions", {"storage"}}};
    }
    HostRig rig(block);
    rig.host->load_from(tmp.path.string());
    helix::LogCapture logs;
    CHECK(pm::resolve(pm::MenuKind::Spool, json::object()).empty());
    for (const auto& c : cases) {
        INFO(c.id);
        LuaRuntime* rt = rig.host->runtime(c.id);
        REQUIRE(rt);
        CHECK_FALSE(rt->faulted());
        CHECK(dispatch_ui_handler(*rt, "probe", std::nullopt));
        lua_getglobal(rt->state(), "probe");
        CHECK(lua_isnil(rt->state(), -1));
        lua_pop(rt->state(), 1);
    }
    CHECK(logs.count_containing("must return a boolean") == 3);
    CHECK(logs.count_containing("from __index") == 0);
}

TEST_CASE_METHOD(LVGLTestFixture, "a check result is refused for its metatable alone",
                 "[plugin][menu]") {
    // Valid fields read raw, and a metatable with no metamethods: only the metatable
    // rule hides it. A reason that is a table is refused by type, never converted.
    TempDir tmp;
    write_menu_plugin(tmp.path, "a-plug", "Ay",
                      spool_item("bare", R"(,"check":"bare")") + "," +
                          spool_item("why", R"(,"check":"why")") + "," + spool_item("fine"),
                      R"(
        helix.ui.on("run", function() end)
        helix.ui.on("bare", function()
            return setmetatable({visible = true, enabled = true}, {})
        end)
        helix.ui.on("why", function()
            return {visible = true, enabled = false,
                    reason = setmetatable({}, {__tostring = function() return "x" end})}
        end)
    )");
    HostRig rig(enabled("a-plug", {}));
    rig.host->load_from(tmp.path.string());
    CHECK(item_ids(pm::resolve(pm::MenuKind::Spool, json::object())) ==
          std::vector<std::string>{"a-plug/fine"});
}

#endif // HELIX_HAS_PLUGINS
