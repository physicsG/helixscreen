// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "ui_update_queue.h"

#include "../lvgl_test_fixture.h"
#include "../test_helpers/plugin_test_support.h"
#include "lua_bindings.h"
#include "plugin_backend.h"

#include "../catch_amalgamated.hpp"

using namespace helix::plugin;
using namespace helix::plugin::test;

namespace {
void drain() {
    helix::ui::UpdateQueue::instance().drain();
}
} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "subscribe publishes the plugin's objects",
                 "[plugin][lua][subscribe]") {
    BoundRuntime b({&install_moonraker_bindings});
    REQUIRE(b.t.run(R"(
        h = helix.moonraker.subscribe({ extruder = { "temperature" } }, function(s) end)
    )"));
    REQUIRE(b.fake.object_sets.size() == 1);
    CHECK(b.fake.object_sets.back() ==
          std::make_pair(std::string("test-plugin"), json{{"extruder", {"temperature"}}}));
}

TEST_CASE_METHOD(LVGLTestFixture, "the first callback carries queried values",
                 "[plugin][lua][subscribe]") {
    BoundRuntime b({&install_moonraker_bindings});
    REQUIRE(b.t.run(R"(
        temp = "none"
        target = "unset"
        calls = 0
        helix.moonraker.subscribe({ extruder = { "temperature" } }, function(s)
            calls = calls + 1
            temp = s.extruder and s.extruder.temperature
            target = s.extruder and s.extruder.target
        end)
    )"));
    REQUIRE(b.fake.requests.size() == 1);
    CHECK(b.fake.requests[0].a == "printer.objects.query");
    CHECK(b.fake.requests[0].params == json{{"objects", {{"extruder", {"temperature"}}}}});
    b.fake.requests[0].reply(RpcResult{
        true, json{{"status", {{"extruder", {{"temperature", 210.5}, {"target", 215}}}}}}, {}});
    drain();
    CHECK(b.t.global("calls") == "1");
    CHECK(b.t.global("temp") == "210.5");
    CHECK(b.t.global("target") == "nil");
}

TEST_CASE_METHOD(LVGLTestFixture, "deltas reach only the matching subscription",
                 "[plugin][lua][subscribe]") {
    BoundRuntime b({&install_moonraker_bindings});
    REQUIRE(b.t.run(R"(
        got = {}
        helix.moonraker.subscribe({ extruder = true }, function(s)
            got[#got + 1] = "ext:" .. tostring(s.extruder and s.extruder.temperature)
        end)
        helix.moonraker.subscribe({ heater_bed = { "temperature" } }, function(s)
            got[#got + 1] = "bed:" .. tostring(s.heater_bed and s.heater_bed.temperature)
        end)
    )"));
    REQUIRE(b.fake.notify.size() == 1);
    CHECK(b.fake.notify[0].first == "notify_status_update");
    for (auto& r : b.fake.requests)
        r.reply(RpcResult{true, json{{"status", json::object()}}, {}});
    drain();
    REQUIRE(b.t.run("got = {}"));

    b.fake.notify[0].second(json{{"params", {{{"heater_bed", {{"temperature", 60.0}}}}, 12.3}}});
    drain();
    REQUIRE(b.t.run("r = table.concat(got, ',')"));
    CHECK(b.t.global("r") == "bed:60.0");
}

TEST_CASE_METHOD(LVGLTestFixture, "a true subscription sees whole objects",
                 "[plugin][lua][subscribe]") {
    BoundRuntime b({&install_moonraker_bindings});
    REQUIRE(b.t.run(R"(
        fields = "none"
        helix.moonraker.subscribe({ extruder = true }, function(s)
            fields = s.extruder and (tostring(s.extruder.temperature) .. "/" ..
                                     tostring(s.extruder.power))
        end)
    )"));
    REQUIRE(b.fake.notify.size() == 1);
    b.fake.notify[0].second(
        json{{"params", {{{"extruder", {{"temperature", 30.0}, {"power", 0.4}}}}, 1.0}}});
    drain();
    CHECK(b.t.global("fields") == "30.0/0.4");
}

TEST_CASE_METHOD(LVGLTestFixture, "cancel shrinks the set; closing the runtime clears it",
                 "[plugin][lua][subscribe]") {
    BoundRuntime b({&install_moonraker_bindings});
    REQUIRE(b.t.run(R"(
        a = helix.moonraker.subscribe({ extruder = { "temperature" } }, function(s) end)
        w = helix.moonraker.subscribe({ heater_bed = true }, function(s) end)
    )"));
    REQUIRE(b.fake.object_sets.back().second ==
            json{{"extruder", {"temperature"}}, {"heater_bed", nullptr}});
    REQUIRE(b.t.run("a:cancel()"));
    REQUIRE(b.fake.object_sets.back().second == json{{"heater_bed", nullptr}});
    b.t.rt.reset();
    REQUIRE(b.fake.object_sets.back() ==
            std::make_pair(std::string("test-plugin"), json::object()));
    CHECK(b.fake.notify_unregistered == 1);
}

TEST_CASE_METHOD(LVGLTestFixture, "a cancelled subscription stops delivering",
                 "[plugin][lua][subscribe]") {
    BoundRuntime b({&install_moonraker_bindings});
    REQUIRE(b.t.run(R"(
        n = 0
        h = helix.moonraker.subscribe({ extruder = true }, function(s) n = n + 1 end)
        h:cancel()
        h:cancel()
    )"));
    REQUIRE(b.fake.notify.size() == 1);
    b.fake.notify[0].second(json{{"params", {{{"extruder", {{"temperature", 30.0}}}}, 1.0}}});
    drain();
    for (auto& r : b.fake.requests)
        r.reply(RpcResult{true, json{{"status", {{"extruder", {{"temperature", 30.0}}}}}}, {}});
    drain();
    CHECK(b.t.global("n") == "0");
}

TEST_CASE_METHOD(LVGLTestFixture, "subscribe enforces its limits", "[plugin][lua][subscribe]") {
    BoundRuntime o({&install_moonraker_bindings});
    REQUIRE(o.t.run(R"(
        t = {}
        for i = 1, 17 do t["obj" .. i] = true end
        ok, err = pcall(helix.moonraker.subscribe, t, function() end)
    )"));
    CHECK(o.t.global("ok") == "false");
    CHECK(o.t.global("err").find("at most 16") != std::string::npos);
    CHECK(o.fake.object_sets.empty());

    BoundRuntime s({&install_moonraker_bindings});
    REQUIRE(s.t.run(R"(
        for i = 1, 8 do helix.moonraker.subscribe({ ["o" .. i] = true }, function() end) end
        ok, err = pcall(helix.moonraker.subscribe, { z = true }, function() end)
    )"));
    CHECK(s.t.global("ok") == "false");
    CHECK(s.t.global("err").find("at most 8") != std::string::npos);

    BoundRuntime n({&install_moonraker_bindings});
    REQUIRE(n.t.run(R"(
        ok, err = pcall(helix.moonraker.subscribe,
                        { [string.rep("x", 65)] = true }, function() end)
    )"));
    CHECK(n.t.global("ok") == "false");
    CHECK(n.t.global("err").find("at most 64") != std::string::npos);

    BoundRuntime v({&install_moonraker_bindings});
    REQUIRE(v.t.run(R"(
        fields = {}
        for i = 1, 33 do fields[i] = "f" .. i end
        ok, err = pcall(helix.moonraker.subscribe, { extruder = fields }, function() end)
        ok2, err2 = pcall(helix.moonraker.subscribe, { extruder = { 123 } }, function() end)
        ok3, err3 = pcall(helix.moonraker.subscribe, { extruder = false }, function() end)
        ok4, err4 = pcall(helix.moonraker.subscribe, "extruder", function() end)
    )"));
    CHECK(v.t.global("ok") == "false");
    CHECK(v.t.global("err").find("at most 32 fields") != std::string::npos);
    CHECK(v.t.global("ok2") == "false");
    CHECK(v.t.global("err2").find("field names must be strings") != std::string::npos);
    CHECK(v.t.global("ok3") == "false");
    CHECK(v.t.global("err3").find("values are true or a list of field names") != std::string::npos);
    CHECK(v.t.global("ok4") == "false");
    CHECK(v.fake.object_sets.empty());
}

TEST_CASE_METHOD(LVGLTestFixture, "the object registry unions plugins",
                 "[plugin][lua][subscribe]") {
    publish_plugin_objects("plugin-a", json{{"extruder", {"temperature"}}});
    publish_plugin_objects("plugin-b", json{{"heater_bed", nullptr}, {"toolhead", {"position"}}});
    json u = plugin_objects_union();
    CHECK(u.contains("extruder"));
    CHECK(u.contains("heater_bed"));
    CHECK(u["heater_bed"].is_null());
    CHECK(u.contains("toolhead"));
    publish_plugin_objects("plugin-a", json::object());
    u = plugin_objects_union();
    CHECK_FALSE(u.contains("extruder"));
    CHECK(u.contains("heater_bed"));
    publish_plugin_objects("plugin-b", json::object());
    CHECK(plugin_objects_union().empty());
}

#endif // HELIX_HAS_PLUGINS
