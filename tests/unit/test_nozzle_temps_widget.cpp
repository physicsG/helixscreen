// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_nozzle_temps_widget.cpp
 * @brief Tests for NozzleTempsWidget registration and metadata
 *
 * Verifies that the nozzle_temps widget is registered correctly in the
 * panel widget registry with expected metadata and hardware gating.
 */

#include "panel_widget_registry.h"
#include "src/ui/panel_widgets/nozzle_temps_widget.h"
#include "tool_state.h"

#include <cstring>

#include "../catch_amalgamated.hpp"

using namespace helix;

TEST_CASE("NozzleTempsWidget: widget def exists in registry", "[nozzle_temps][panel_widget]") {
    const auto* def = find_widget_def("nozzle_temps");
    REQUIRE(def != nullptr);

    SECTION("has correct id") {
        REQUIRE(std::strcmp(def->id, "nozzle_temps") == 0);
    }

    SECTION("has display name") {
        REQUIRE(def->display_name != nullptr);
        REQUIRE(std::strlen(def->display_name) > 0);
    }

    SECTION("has icon") {
        REQUIRE(def->icon != nullptr);
        REQUIRE(std::strcmp(def->icon, "thermometer") == 0);
    }

    SECTION("has description") {
        REQUIRE(def->description != nullptr);
        REQUIRE(std::strlen(def->description) > 0);
    }
}

TEST_CASE("NozzleTempsWidget: no hardware gate (visible to all printers)",
          "[nozzle_temps][panel_widget]") {
    const auto* def = find_widget_def("nozzle_temps");
    REQUIRE(def != nullptr);
    REQUIRE(def->hardware_gate_subject == nullptr);
}

TEST_CASE("NozzleTempsWidget: default rowspan is two cells (1x2 preferred)",
          "[nozzle_temps][panel_widget]") {
    const auto* def = find_widget_def("nozzle_temps");
    REQUIRE(def != nullptr);

    // Tracks, not cells — a track is half a cell (GridLayout::TRACKS_PER_CELL),
    // so one authored cell is 2 here.
    REQUIRE(def->colspan == 2);
    REQUIRE(def->rowspan == 4);

    // Can scale from one cell to 2x3 cells (supports the compact and 2x1 layouts)
    REQUIRE(def->effective_min_colspan() == 2);
    REQUIRE(def->effective_min_rowspan() == 2);
    REQUIRE(def->effective_max_colspan() == 4);
    REQUIRE(def->effective_max_rowspan() == 6);
}

TEST_CASE("NozzleTempsWidget: not enabled by default (requires multi-tool)",
          "[nozzle_temps][panel_widget]") {
    const auto* def = find_widget_def("nozzle_temps");
    REQUIRE(def != nullptr);
    REQUIRE_FALSE(def->default_enabled);
}

// Regression: an AFC BoxTurtle (and Happy Hare / ERCF) is one physical extruder
// fed by N spool lanes. ToolState models the lanes as N logical tools (T0..Tn)
// that all carry extruder_name="extruder", so iterating tools() builds N
// identical nozzle rows — the widget showed "150°" four times. The widget must
// collapse to distinct physical extruders.
TEST_CASE("NozzleTempsWidget: collapses multiplexed lanes to one nozzle row",
          "[nozzle_temps][panel_widget][afc]") {
    SECTION("AFC: 4 lanes feeding one extruder -> one row") {
        std::vector<ToolInfo> tools;
        for (int i = 0; i < 4; ++i) {
            ToolInfo t;
            t.index = i;
            t.name = "T" + std::to_string(i);
            t.extruder_name = "extruder"; // all lanes share the single extruder
            tools.push_back(std::move(t));
        }
        auto extruders = distinct_extruder_names(tools);
        REQUIRE(extruders.size() == 1);
        REQUIRE(extruders[0] == "extruder");
    }

    SECTION("Toolchanger: 4 distinct extruders -> four rows, order preserved") {
        std::vector<ToolInfo> tools;
        const char* names[] = {"extruder", "extruder1", "extruder2", "extruder3"};
        for (int i = 0; i < 4; ++i) {
            ToolInfo t;
            t.index = i;
            t.name = "T" + std::to_string(i);
            t.extruder_name = names[i];
            tools.push_back(std::move(t));
        }
        auto extruders = distinct_extruder_names(tools);
        REQUIRE(extruders.size() == 4);
        REQUIRE(extruders[0] == "extruder");
        REQUIRE(extruders[1] == "extruder1");
        REQUIRE(extruders[2] == "extruder2");
        REQUIRE(extruders[3] == "extruder3");
    }

    SECTION("Tools without an extruder mapping are dropped") {
        std::vector<ToolInfo> tools(2);
        tools[0].extruder_name = "extruder";
        tools[1].extruder_name = std::nullopt;
        auto extruders = distinct_extruder_names(tools);
        REQUIRE(extruders.size() == 1);
        REQUIRE(extruders[0] == "extruder");
    }

    SECTION("Empty tool list -> no rows") {
        REQUIRE(distinct_extruder_names({}).empty());
    }
}
