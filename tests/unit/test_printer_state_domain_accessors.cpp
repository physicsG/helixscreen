// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../test_helpers/printer_state_test_access.h"
#include "printer_state.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

// Each accessor must hand back the very member PrinterState drives, not a copy
// and not a sibling domain of the same shape.
TEST_CASE("PrinterState domain accessors return the owned members",
          "[printer_state][domain_accessors]") {
    PrinterState ps;
    const PrinterState& cps = ps;
    const auto members = PrinterStateTestAccess::domain_members(ps);

    const std::vector<std::pair<const void*, const void*>> accessed = {
        {&ps.temperature_state(), &cps.temperature_state()},
        {&ps.motion_state(), &cps.motion_state()},
        {&ps.fan_state(), &cps.fan_state()},
        {&ps.print_state(), &cps.print_state()},
        {&ps.capabilities_state(), &cps.capabilities_state()},
        {&ps.plugin_status_state(), &cps.plugin_status_state()},
        {&ps.calibration_state(), &cps.calibration_state()},
        {&ps.hardware_validation_state(), &cps.hardware_validation_state()},
        {&ps.composite_visibility_state(), &cps.composite_visibility_state()},
        {&ps.network_state(), &cps.network_state()},
        {&ps.versions_state(), &cps.versions_state()},
        {&ps.excluded_objects_state(), &cps.excluded_objects_state()},
        {&ps.profile_state(), &cps.profile_state()},
    };

    REQUIRE(accessed.size() == members.size());
    for (size_t i = 0; i < members.size(); ++i) {
        INFO("domain " << i);
        CHECK(accessed[i].first == members[i]);
        CHECK(accessed[i].second == members[i]);
    }
}
