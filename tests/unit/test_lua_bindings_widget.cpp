// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "../test_helpers/plugin_test_support.h"
#include "lua_bindings.h"

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

#endif // HELIX_HAS_PLUGINS
