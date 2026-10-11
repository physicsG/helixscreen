// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "ui_update_queue.h"

#include "app_globals.h"
#include "http_executor.h"
#include "plugin_backend.h"

#include <chrono>
#include <future>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::plugin;

TEST_CASE("app backend reports no connection instead of crashing", "[plugin][backend]") {
    REQUIRE(get_moonraker_api() == nullptr);
    REQUIRE(get_moonraker_client() == nullptr);
    auto b = make_app_backend();
    std::vector<RpcResult> got;
    auto sink = [&](RpcResult r) { got.push_back(std::move(r)); };
    b.gcode("M117 hi", sink);
    b.call("server.info", json::object(), sink);
    b.upload("gcodes", "a.gcode", "G28", sink);
    b.download("gcodes", "a.gcode", 1024, sink);
    REQUIRE(got.size() == 4);
    for (const auto& r : got) {
        CHECK_FALSE(r.ok);
        CHECK(r.error.find("not connected") != std::string::npos);
    }
    auto off = b.on_notify("notify_agent_event", [](const json&) {});
    REQUIRE(off);
    off();
}

TEST_CASE("app backend rejects roots other than gcodes and config", "[plugin][backend]") {
    auto b = make_app_backend();
    RpcResult up, down;
    b.upload("logs", "x", "y", [&](RpcResult r) { up = std::move(r); });
    b.download("../etc", "x", 1024, [&](RpcResult r) { down = std::move(r); });
    CHECK_FALSE(up.ok);
    CHECK(up.error.find("root") != std::string::npos);
    CHECK_FALSE(down.ok);
}

TEST_CASE("plan_http_target refuses every spelling of this machine", "[plugin][backend]") {
    const std::vector<std::string> none;
    for (const char* url :
         {"http://:7125/x", "http:///x", "http://evil.com?@127.0.0.1:7125/x",
          "http://evil.com#@127.0.0.1:7125/x", "http://0.0.0.0:7125/x", "http://[::]:7125/x",
          "http://127.1/x", "http://2130706433/x", "http://[::1]/x", "http://[::ffff:127.0.0.1]/x",
          "http://localhost:7125/x", "https://127.0.0.1/"}) {
        INFO(url);
        HttpTarget t = plan_http_target(url, none);
        CHECK_FALSE(t.ok);
        CHECK(t.error.find("printer host") != std::string::npos);
    }
}

TEST_CASE("plan_http_target refuses an address in the forbidden list", "[plugin][backend]") {
    HttpTarget t = plan_http_target("http://93.184.216.34/x", {"10.0.0.2", "93.184.216.34"});
    CHECK_FALSE(t.ok);
    CHECK(t.error.find("printer host") != std::string::npos);
}

TEST_CASE("plan_http_target pins a plain http connect to the checked address",
          "[plugin][backend]") {
    HttpTarget t = plan_http_target("http://93.184.216.34:8080/a?b=1", {"10.0.0.2"});
    REQUIRE(t.ok);
    CHECK(t.connect_url == "http://93.184.216.34:8080/a?b=1");
    CHECK(t.host_header == "93.184.216.34:8080");

    HttpTarget v6 = plan_http_target("http://[2606:4700::1111]/p", {});
    REQUIRE(v6.ok);
    CHECK(v6.connect_url == "http://[2606:4700::1111]:80/p");
}

TEST_CASE("plan_http_target leaves an https URL unpinned", "[plugin][backend]") {
    HttpTarget t = plan_http_target("https://93.184.216.34/x", {});
    REQUIRE(t.ok);
    CHECK(t.connect_url == "https://93.184.216.34/x");
    CHECK(t.host_header.empty());
}

TEST_CASE("app backend http refuses the printer's own host", "[plugin][backend]") {
    helix::http::HttpExecutor::slow().start();
    auto b = make_app_backend();
    std::promise<RpcResult> done;
    auto ready = done.get_future();
    b.http("GET", "http://127.0.0.1:7125/x", "", json::object(), 5000, 1024,
           [&done](RpcResult r) { done.set_value(std::move(r)); });
    REQUIRE(ready.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    RpcResult r = ready.get();
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("printer host") != std::string::npos);
    helix::http::HttpExecutor::slow().stop();
}

#endif // HELIX_HAS_PLUGINS

TEST_CASE("app backend set_plugin_objects reaches the object union", "[plugin][backend]") {
    auto b = make_app_backend();

    // The registry behind plugin_objects_union() is process-wide, so the probe
    // entry is cleared before the case ends whatever its assertions did.
    b.set_plugin_objects("union_probe_plugin", json{{"union_probe_object", {"field"}}});
    const json unioned = plugin_objects_union();
    CHECK(unioned.contains("union_probe_object"));

    // An empty object clears the plugin's entry.
    b.set_plugin_objects("union_probe_plugin", json::object());
    CHECK_FALSE(plugin_objects_union().contains("union_probe_object"));

    // Publishing schedules a coalesced subscription refresh on the UI queue.
    helix::ui::UpdateQueue::instance().drain();
}
