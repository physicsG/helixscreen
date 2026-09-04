// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_tool_switcher_widget.cpp
 * @brief Tests for ToolSwitcherWidget registration and metadata
 *
 * Verifies that the tool_switcher widget is registered correctly in the
 * panel widget registry with expected metadata and hardware gating.
 */

#include "panel_widget_registry.h"
#include "tool_state.h"

#include <cstring>

#include "../catch_amalgamated.hpp"

using namespace helix;

TEST_CASE("ToolSwitcherWidget: widget def exists in registry", "[tool_switcher][panel_widget]") {
    const auto* def = find_widget_def("tool_switcher");
    REQUIRE(def != nullptr);

    SECTION("has correct id") {
        REQUIRE(std::strcmp(def->id, "tool_switcher") == 0);
    }

    SECTION("has display name") {
        REQUIRE(def->display_name != nullptr);
        REQUIRE(std::strlen(def->display_name) > 0);
    }

    SECTION("has icon") {
        REQUIRE(def->icon != nullptr);
        REQUIRE(std::strlen(def->icon) > 0);
    }

    SECTION("has description") {
        REQUIRE(def->description != nullptr);
        REQUIRE(std::strlen(def->description) > 0);
    }
}

TEST_CASE("ToolSwitcherWidget: no hardware gate (visible to all printers)",
          "[tool_switcher][panel_widget]") {
    const auto* def = find_widget_def("tool_switcher");
    REQUIRE(def != nullptr);
    REQUIRE(def->hardware_gate_subject == nullptr);
}

TEST_CASE("ToolSwitcherWidget: supports scaling from one cell to 2x2 cells",
          "[tool_switcher][panel_widget]") {
    const auto* def = find_widget_def("tool_switcher");
    REQUIRE(def != nullptr);

    // Tracks, not cells — a track is half a cell (GridLayout::TRACKS_PER_CELL).
    // Default is one whole cell (compact).
    REQUIRE(def->colspan == 2);
    REQUIRE(def->rowspan == 2);

    // Can scale up to 2x2 cells
    REQUIRE(def->effective_max_colspan() == 4);
    REQUIRE(def->effective_max_rowspan() == 4);

    // Minimum is one whole cell
    REQUIRE(def->effective_min_colspan() == 2);
    REQUIRE(def->effective_min_rowspan() == 2);

    REQUIRE(def->is_scalable());
}

TEST_CASE("ToolSwitcherWidget: not enabled by default (requires multi-tool)",
          "[tool_switcher][panel_widget]") {
    const auto* def = find_widget_def("tool_switcher");
    REQUIRE(def != nullptr);
    REQUIRE_FALSE(def->default_enabled);
}
