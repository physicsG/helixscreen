// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_queue_stats_registry.cpp
 * @brief Registry rows for the job queue and print stats tiles: both go down to
 *        a 2x1 strip, the queue tile is gated on Moonraker's job_queue.
 */

#include "panel_widget_registry.h"

#include <string>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {
const PanelWidgetDef& def_of(const char* id) {
    const PanelWidgetDef* d = find_widget_def(id);
    REQUIRE(d != nullptr);
    return *d;
}
} // namespace

TEST_CASE("queue and stats tiles authored spans", "[widget_def][queue_stats]") {
    for (const char* id : {"job_queue", "print_stats"}) {
        CAPTURE(id);
        const PanelWidgetDef& d = def_of(id);
        CHECK(d.effective_min_colspan() == 2);
        CHECK(d.effective_min_rowspan() == 1);
        CHECK(d.colspan == 4);
        CHECK(d.rowspan == 4);
        CHECK_FALSE(d.default_enabled);
        CHECK(d.supports_half_col);
        CHECK(d.supports_half_row);
    }
}

TEST_CASE("job queue tile is gated on Moonraker's job_queue", "[widget_def][queue_stats]") {
    const PanelWidgetDef& d = def_of("job_queue");
    REQUIRE(d.hardware_gate_subject != nullptr);
    CHECK(std::string(d.hardware_gate_subject) == "printer_has_job_queue");
    REQUIRE(d.hardware_gate_hint != nullptr);
    CHECK(std::string(d.hardware_gate_hint) == "Job queue not enabled in Moonraker");
}

TEST_CASE("print stats tile has no gate and no success rate", "[widget_def][queue_stats]") {
    const PanelWidgetDef& d = def_of("print_stats");
    CHECK(d.hardware_gate_subject == nullptr);
    CHECK(std::string(d.description).find("success") == std::string::npos);
}
