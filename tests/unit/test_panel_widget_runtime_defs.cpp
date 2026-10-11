// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "../test_helpers/process_async_timers.h"
#include "../test_helpers/scope_exit.h"
#include "misc/lv_timer_private.h"
#include "panel_widget_manager.h"
#include "panel_widget_registry.h"

#include <string>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {} // namespace

TEST_CASE("runtime widget defs register, list and unregister", "[widget_registry]") {
    size_t base = get_all_widget_defs().size();
    RuntimeWidgetDef d;
    d.id = "demo-plug_tile";
    d.display_name = "Demo";
    d.icon = "puzzle_outline";
    d.description = "A plugin tile";
    helix::test::ScopeExit cleanup([] { unregister_runtime_widget_def("demo-plug_tile"); });
    REQUIRE(register_runtime_widget_def(d));

    const PanelWidgetDef* def = find_widget_def("demo-plug_tile");
    REQUIRE(def);
    CHECK(def->category == WidgetCategory::Plugins);
    CHECK_FALSE(def->default_enabled);
    CHECK_FALSE(def->multi_instance);
    CHECK(std::string(def->display_name) == "Demo");
    CHECK(get_all_widget_defs().size() == base + 1);

    const char* id_before = def->id;
    d.display_name = "Renamed";
    REQUIRE(register_runtime_widget_def(d)); // replaces, keeps the id storage
    CHECK(get_all_widget_defs().size() == base + 1);
    CHECK(find_widget_def("demo-plug_tile")->id == id_before);

    unregister_runtime_widget_def("demo-plug_tile");
    CHECK(find_widget_def("demo-plug_tile") == nullptr);
    CHECK(get_all_widget_defs().size() == base);
}

TEST_CASE("a runtime def cannot take a built-in id", "[widget_registry]") {
    RuntimeWidgetDef d;
    d.id = "print_status";
    d.display_name = "X";
    CHECK_FALSE(register_runtime_widget_def(d));
    CHECK(find_widget_def("print_status")->category == WidgetCategory::PrintStatus);
}

TEST_CASE_METHOD(LVGLTestFixture, "changed definitions rebuild every widget list",
                 "[widget_registry]") {
    // Panels list widgets through their gate-observer rebuild slot, so both a
    // home panel and a catalog register one; notify schedules an async
    // rebuild per panel rather than rebuilding inline.
    int home = 0, catalog = 0;
    auto& mgr = PanelWidgetManager::instance();
    mgr.setup_gate_observers("test_home", [&] { ++home; });
    mgr.setup_gate_observers("test_catalog", [&] { ++catalog; });
    mgr.notify_widget_defs_changed();
    process_lvgl(50);
    process_async_timers();
    CHECK(home == 1);
    CHECK(catalog == 1);
    PanelWidgetManager::clear_gate_observers("test_home");
    PanelWidgetManager::clear_gate_observers("test_catalog");
}

TEST_CASE("the Plugins category exists and comes last", "[widget_registry]") {
    const auto& cats = get_widget_categories();
    REQUIRE_FALSE(cats.empty());
    CHECK(cats.back().id == WidgetCategory::Plugins);
    CHECK(std::string(cats.back().icon) == "puzzle_outline");
}
