// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "../lvgl_test_fixture.h"
#include "../test_helpers/plugin_host_test_support.h"
#include "../test_helpers/plugin_test_support.h"
#include "plugin_host.h"

#include <filesystem>
#include <fstream>

#include "../catch_amalgamated.hpp"

using namespace helix::plugin;
using namespace helix::plugin::test;
namespace fs = std::filesystem;

namespace {

void copy_fixture(const fs::path& to, const std::string& name) {
    fs::copy(fs::path("tests/fixtures/plugins") / name, to / name, fs::copy_options::recursive);
}

void write(const fs::path& p, const std::string& s) {
    std::ofstream(p, std::ios::trunc) << s;
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "rescan reloads only the named plugin",
                 "[plugin][host][rescan]") {
    TempDir tmp;
    copy_fixture(tmp.path, "hello");
    copy_fixture(tmp.path, "widget-demo");
    HostRig rig(enabled_all({{"hello", {"gcode"}}, {"widget-demo", {}}}));
    rig.host->load_from(tmp.path.string());
    LuaRuntime* hello_before = rig.host->runtime("hello");
    LuaRuntime* demo_before = rig.host->runtime("widget-demo");
    REQUIRE(hello_before != nullptr);
    REQUIRE(demo_before != nullptr);

    write(tmp.path / "hello" / "main.lua", "reloaded = true\n");
    rig.host->rescan({"hello"});
    drain();

    CHECK(rig.host->runtime("widget-demo") == demo_before);
    LuaRuntime* hello_after = rig.host->runtime("hello");
    REQUIRE(hello_after != nullptr);
    // A fresh runtime can land on the old one's address, so the reload shows
    // through the new main.lua's global, not through pointer identity.
    lua_getglobal(hello_after->state(), "reloaded");
    CHECK((lua_isboolean(hello_after->state(), -1) && lua_toboolean(hello_after->state(), -1)));
    lua_pop(hello_after->state(), 1);
}

TEST_CASE_METHOD(LVGLTestFixture, "rescan adds a new plugin and removes a deleted one",
                 "[plugin][host][rescan]") {
    TempDir tmp;
    copy_fixture(tmp.path, "hello");
    HostRig rig(enabled_all({{"hello", {"gcode"}}, {"widget-demo", {}}}));
    rig.host->load_from(tmp.path.string());
    REQUIRE(rig.host->plugins().size() == 1);

    copy_fixture(tmp.path, "widget-demo");
    fs::remove_all(tmp.path / "hello");
    rig.host->rescan({"hello", "widget-demo"});
    drain();

    REQUIRE(rig.host->plugins().size() == 1);
    CHECK(rig.host->plugins()[0].dir_name == "widget-demo");
    CHECK(rig.host->runtime("hello") == nullptr);
    CHECK(rig.host->runtime("widget-demo") != nullptr);
}

TEST_CASE_METHOD(LVGLTestFixture, "rescan of an update that grows permissions needs approval",
                 "[plugin][host][rescan]") {
    TempDir tmp;
    copy_fixture(tmp.path, "hello");
    HostRig rig(enabled("hello", {"gcode"}));
    rig.host->load_from(tmp.path.string());
    REQUIRE(rig.host->runtime("hello") != nullptr);

    // Same plugin, now asking for http too, which was never granted.
    auto mpath = tmp.path / "hello" / "manifest.json";
    json m = json::parse(read_text(mpath), nullptr, false);
    REQUIRE(m.is_object());
    m["permissions"] = json::array({"gcode", "http"});
    write(mpath, m.dump());
    rig.host->rescan({"hello"});
    drain();

    CHECK(rig.host->runtime("hello") == nullptr);
    CHECK(rig.host->plugins()[0].status == PluginStatus::NeedsApproval);
}

TEST_CASE_METHOD(LVGLTestFixture, "rescan of an invalid manifest keeps the entry with its error",
                 "[plugin][host][rescan]") {
    TempDir tmp;
    copy_fixture(tmp.path, "hello");
    HostRig rig(enabled("hello", {"gcode"}));
    rig.host->load_from(tmp.path.string());
    write(tmp.path / "hello" / "manifest.json", "{ not json");
    rig.host->rescan({"hello"});
    drain();
    REQUIRE(rig.host->plugins().size() == 1);
    CHECK(rig.host->plugins()[0].status == PluginStatus::Invalid);
    CHECK(rig.host->runtime("hello") == nullptr);
}

TEST_CASE_METHOD(LVGLTestFixture, "rescan ignores ids that are not valid plugin ids",
                 "[plugin][host][rescan]") {
    TempDir tmp;
    copy_fixture(tmp.path, "hello");
    HostRig rig(enabled("hello", {"gcode"}));
    rig.host->load_from(tmp.path.string());
    REQUIRE(rig.host->runtime("hello") != nullptr);

    rig.host->rescan({"../escape"});
    drain();

    REQUIRE(rig.host->plugins().size() == 1);
    CHECK(rig.host->plugins()[0].dir_name == "hello");
    CHECK(rig.host->runtime("hello") != nullptr);
}

#endif // HELIX_HAS_PLUGINS
