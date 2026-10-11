// tests/test_helpers/pre_print_option_sets.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pre_print_option.h"

#include <vector>

namespace helix::test {

/// A two-option set in the shape the print-detail panel renders: one "skip"
/// option that ships enabled, one "add-on" that ships disabled. Built directly
/// rather than pulled from printer_database.json so the contracts under test
/// do not move when the shipped DB does.
inline PrePrintOptionSet make_skip_and_addon_set() {
    PrePrintOptionSet set;
    set.macro_name = "START_PRINT";

    PrePrintOption skip;
    skip.id = "bed_mesh";
    skip.category = PrePrintCategory::Mechanical;
    skip.order = 10;
    skip.default_enabled = true; // "don't skip, do what the file says"
    skip.strategy_kind = PrePrintStrategyKind::MacroParam;
    // 4-arg form, as test_pre_print_options_renderer.cpp uses: adaptive_value keeps
    // its "1" default member initializer.
    skip.strategy = PrePrintStrategyMacroParam{"SKIP_BED_MESH", "0", "1", "0"};

    PrePrintOption addon;
    addon.id = "timelapse";
    addon.category = PrePrintCategory::Monitoring;
    addon.order = 10;
    addon.default_enabled = false; // "don't add extras by default"
    addon.strategy_kind = PrePrintStrategyKind::PreStartGcode;
    addon.strategy = PrePrintStrategyPreStartGcode{"TIMELAPSE_RENDER"};

    set.options = {skip, addon};
    return set;
}

} // namespace helix::test
