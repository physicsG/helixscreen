// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_multi_tool_detection.cpp
 * @brief A multi-tool printer is one that has more than one hot end, whoever built it.
 *
 * Tool counts come from three sources, in descending authority: the [tool N]
 * objects klipper-toolchanger creates, a changer extra's own count, and, for a
 * machine presenting one heater and one extruder motor per tool, the extruder
 * heater count. The third is the only signal a printer whose changer ships
 * neither klipper-toolchanger nor a recognised extra gives off, and it is a
 * capability question: the machine has N hot ends, so it has N tools.
 *
 * Registering AmsType::TOOL_CHANGER for those is what gives them slots, per-tool
 * spool identity and the filament panel's tool selector. Without it the panel
 * falls back to the single-extruder path, which is the wrong UI for a six-tool
 * machine (prestonbrown/helixscreen#1350).
 *
 * What command performs the swap on such a machine is the other half, and it
 * lives with the rest of the provider-table tests in
 * tests/unit/test_toolchanger_tool_source.cpp.
 */

#include "printer_discovery.h"
#include "toolchanger_addon.h"

#include <json.hpp> // nlohmann/json from libhv

#include "../catch_amalgamated.hpp"

using json = nlohmann::json;
using namespace helix;

namespace {

/// A dual-extruder machine with no filament system and no changer module: two
/// hot ends, T0/T1 macros, nothing else to go on.
json dual_extruder_objects() {
    return json::array({"extruder", "extruder1", "gcode_macro T0", "gcode_macro T1", "heater_bed",
                        "toolhead", "gcode_move", "configfile"});
}

} // namespace

// ============================================================================
// Tool enumeration
// ============================================================================

TEST_CASE("extruder heaters name the tools when no tool objects exist",
          "[printer_discovery][multitool]") {
    PrinterDiscovery hw;
    hw.parse_objects(dual_extruder_objects());

    REQUIRE(hw.tool_names().size() == 2);
    CHECK(hw.tool_names()[0] == "T0");
    CHECK(hw.tool_names()[1] == "T1");
}

TEST_CASE("a single hot end is not a multi-tool printer", "[printer_discovery][multitool]") {
    PrinterDiscovery hw;
    hw.parse_objects(json::array({"extruder", "heater_bed", "gcode_macro T0", "toolhead"}));

    CHECK(hw.tool_names().empty());
    CHECK(hw.mmu_type() == AmsType::NONE);
    CHECK(hw.detected_ams_systems().empty());
}

TEST_CASE("a mixing hotend's extruder_stepper is not a second tool",
          "[printer_discovery][multitool]") {
    // Chimera/cyclops: one [extruder] heater plus [extruder_stepper] sections.
    // One hot end, so one tool, and the enumeration must not see the steppers.
    PrinterDiscovery hw;
    hw.parse_objects(json::array(
        {"extruder", "extruder_stepper e1", "extruder_stepper e2", "heater_bed", "toolhead"}));

    CHECK(hw.tool_names().empty());
    CHECK(hw.detected_ams_systems().empty());
}

// ============================================================================
// AMS registration
// ============================================================================

TEST_CASE("a multi-extruder printer with no filament system gets a tool changer AMS",
          "[printer_discovery][multitool]") {
    PrinterDiscovery hw;
    hw.parse_objects(dual_extruder_objects());

    REQUIRE(hw.mmu_type() == AmsType::TOOL_CHANGER);
    REQUIRE(hw.detected_ams_systems().size() == 1);
    CHECK(hw.detected_ams_systems()[0].type == AmsType::TOOL_CHANGER);
    // Nothing here is a MedusaHC. The general rule must not reach the vendor
    // provider table, which drives dock sensors and a feeder this machine lacks.
    CHECK_FALSE(helix::toolchanger_addon::present(hw));
}

TEST_CASE("a filament system on a multi-extruder machine keeps its own backend",
          "[printer_discovery][multitool]") {
    PrinterDiscovery hw;
    hw.parse_objects(json::array({"mmu", "extruder", "extruder1", "heater_bed", "toolhead"}));

    REQUIRE(hw.mmu_type() == AmsType::HAPPY_HARE);
    REQUIRE(hw.detected_ams_systems().size() == 1);
    CHECK(hw.detected_ams_systems()[0].type == AmsType::HAPPY_HARE);
}

TEST_CASE("real tool objects still name the tools", "[printer_discovery][multitool]") {
    // Six heaters, four tools: the objects win, because a klipper-toolchanger
    // name is arbitrary and ASSIGN_TOOL can remap it.
    PrinterDiscovery hw;
    hw.parse_objects(json::array({"toolchanger", "tool T0", "tool T1", "tool T2", "tool T3",
                                  "extruder", "extruder1", "extruder2", "extruder3", "extruder4",
                                  "extruder5", "toolhead"}));

    REQUIRE(hw.tool_names().size() == 4);
    CHECK(hw.mmu_type() == AmsType::TOOL_CHANGER);
}
