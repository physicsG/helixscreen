// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "../lvgl_test_fixture.h"
#include "../test_helpers/plugin_host_test_support.h"
#include "../test_helpers/plugin_test_support.h"
#include "plugin_dir_watcher.h"

#include <chrono>
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

TEST_CASE_METHOD(LVGLTestFixture, "watcher reloads a plugin whose files changed",
                 "[plugin][watcher]") {
    TempDir tmp;
    copy_fixture(tmp.path, "hello");
    HostRig rig(enabled("hello", {"gcode"}));
    rig.host->load_from(tmp.path.string());
    LuaRuntime* before = rig.host->runtime("hello");
    REQUIRE(before != nullptr);
    PluginDirWatcher watcher(*rig.host, tmp.path.string());

    watcher.poll_now();
    drain();
    CHECK(rig.host->runtime("hello") == before);

    auto main = tmp.path / "hello" / "main.lua";
    write(main, "reloaded = true\n");
    std::error_code ec;
    fs::last_write_time(main, fs::file_time_type::clock::now() + std::chrono::seconds(2), ec);
    REQUIRE(!ec);
    watcher.poll_now();
    drain();

    LuaRuntime* after = rig.host->runtime("hello");
    REQUIRE(after != nullptr);
    // The reload shows through the new main.lua's global: a fresh runtime can
    // land on the old one's address, so pointer identity proves nothing.
    lua_getglobal(after->state(), "reloaded");
    CHECK((lua_isboolean(after->state(), -1) && lua_toboolean(after->state(), -1)));
    lua_pop(after->state(), 1);
}

TEST_CASE_METHOD(LVGLTestFixture, "watcher adds a plugin that appears in the dir",
                 "[plugin][watcher]") {
    TempDir tmp;
    copy_fixture(tmp.path, "hello");
    HostRig rig(enabled_all({{"hello", {"gcode"}}, {"widget-demo", {}}}));
    rig.host->load_from(tmp.path.string());
    REQUIRE(rig.info("hello") != nullptr);
    REQUIRE(rig.info("widget-demo") == nullptr);
    PluginDirWatcher watcher(*rig.host, tmp.path.string());

    copy_fixture(tmp.path, "widget-demo");
    watcher.poll_now();
    drain();

    CHECK(rig.info("widget-demo") != nullptr);
    CHECK(rig.host->runtime("widget-demo") != nullptr);
}

TEST_CASE_METHOD(LVGLTestFixture, "watcher removes a plugin deleted from the dir",
                 "[plugin][watcher]") {
    TempDir tmp;
    copy_fixture(tmp.path, "hello");
    HostRig rig(enabled("hello", {"gcode"}));
    rig.host->load_from(tmp.path.string());
    REQUIRE(rig.info("hello") != nullptr);
    PluginDirWatcher watcher(*rig.host, tmp.path.string());

    fs::remove_all(tmp.path / "hello");
    watcher.poll_now();
    drain();

    CHECK(rig.info("hello") == nullptr);
    CHECK(rig.host->runtime("hello") == nullptr);
}

TEST_CASE_METHOD(LVGLTestFixture, "watcher timer is cancelled on destruction",
                 "[plugin][watcher]") {
    TempDir tmp;
    copy_fixture(tmp.path, "hello");
    HostRig rig(enabled("hello", {"gcode"}));
    rig.host->load_from(tmp.path.string());
    { PluginDirWatcher watcher(*rig.host, tmp.path.string()); }
    // The pass condition is the pump itself: a poll timer still armed over the
    // freed watcher fires into dangling memory two periods later.
    process_lvgl(2000);
    CHECK(rig.host->runtime("hello") != nullptr);
}

#endif // HELIX_HAS_PLUGINS
