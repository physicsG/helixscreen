// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_chamber_heater_assignment.cpp
 * @brief helix::chamber::resolve_heater() and resolve_sensor(): which chamber
 *        heater and chamber sensor a printer has under its chamber assignments.
 *
 * The wiring (PrinterState publishing the answer, and the consumers reading
 * it) is pinned in test_printer_state.cpp, test_temperature_controller.cpp,
 * test_filament_panel_chamber.cpp and test_temperature_service_chamber.cpp.
 */

#include "chamber_heater_assignment.h"
#include "printer_discovery.h"

#include <initializer_list>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using helix::PrinterDiscovery;
using helix::chamber::resolve_heater;
using helix::chamber::resolve_sensor;

namespace {

PrinterDiscovery discovered(std::initializer_list<const char*> objects) {
    PrinterDiscovery hw;
    nlohmann::json list = nlohmann::json::array();
    for (const char* object : objects) {
        list.push_back(object);
    }
    hw.parse_objects(list);
    return hw;
}

/// The name a model preset seeds for its family's chamber heater before the
/// wizard runs, and so before anything has checked the printer has one.
constexpr const char* kPresetChamberHeater = "heater_generic chamber_heater";

} // namespace

TEST_CASE("auto takes the chamber heater discovery picked", "[chamber][assignment]") {
    CHECK(resolve_heater("auto", discovered({"heater_generic chamber", "extruder"})) ==
          "heater_generic chamber");
    CHECK(resolve_heater("auto", discovered({"temperature_fan chamber_fan", "extruder"})) ==
          "temperature_fan chamber_fan");
    CHECK(resolve_heater("auto", discovered({"extruder", "heater_bed"})).empty());
}

TEST_CASE("none disables a chamber heater discovery found", "[chamber][assignment]") {
    const auto hw = discovered({"heater_generic chamber", "extruder", "heater_bed"});
    REQUIRE(hw.has_chamber_heater());
    CHECK(resolve_heater("none", hw).empty());
}

TEST_CASE("a named heater Klipper reports is the chamber heater", "[chamber][assignment]") {
    SECTION("one no chamber keyword names, so only the assignment can pick it") {
        const auto hw = discovered({"heater_generic ptc_heater", "extruder", "heater_bed"});
        REQUIRE_FALSE(hw.has_chamber_heater());
        CHECK(resolve_heater("heater_generic ptc_heater", hw) == "heater_generic ptc_heater");
    }
    SECTION("over the heater discovery picked") {
        const auto hw = discovered({"heater_generic chamber", "heater_generic ptc_heater"});
        REQUIRE(hw.chamber_heater_name() == "heater_generic chamber");
        CHECK(resolve_heater("heater_generic ptc_heater", hw) == "heater_generic ptc_heater");
    }
    SECTION("the preset's own heater on a printer that has it") {
        const auto hw = discovered({kPresetChamberHeater, "temperature_fan chamber_fan"});
        CHECK(resolve_heater(kPresetChamberHeater, hw) == kPresetChamberHeater);
    }
}

TEST_CASE("a named heater Klipper does not report is not the chamber heater",
          "[chamber][assignment]") {
    SECTION("a printer with neither a chamber heater nor a chamber fan has none") {
        const auto hw = discovered({"temperature_sensor chamber_temp", "extruder", "heater_bed"});
        REQUIRE_FALSE(hw.has_chamber_heater());
        CHECK(resolve_heater(kPresetChamberHeater, hw).empty());
    }
    SECTION("a printer whose chamber is driven by a chamber-named temperature_fan gets the fan") {
        const auto hw = discovered({"temperature_fan chamber_fan",
                                    "temperature_sensor chamber_temp", "extruder", "heater_bed"});
        REQUIRE(hw.chamber_heater_name() == "temperature_fan chamber_fan");
        CHECK(resolve_heater(kPresetChamberHeater, hw) == "temperature_fan chamber_fan");
    }
    SECTION("a printer whose chamber heater has another name keeps discovery's pick") {
        const auto hw = discovered({"heater_generic chamber", "extruder", "heater_bed"});
        CHECK(resolve_heater(kPresetChamberHeater, hw) == "heater_generic chamber");
    }
    SECTION("the name must match a reported object exactly") {
        const auto hw = discovered({kPresetChamberHeater, "extruder", "heater_bed"});
        CHECK(resolve_heater("heater_generic chamber", hw) == kPresetChamberHeater);
    }
    SECTION("nothing counts before Klipper has reported its objects") {
        const PrinterDiscovery hw;
        REQUIRE_FALSE(hw.objects_reported());
        CHECK(resolve_heater(kPresetChamberHeater, hw).empty());
    }
}

// ============================================================================
// resolve_sensor()
// ============================================================================

namespace {

/// A chamber sensor name an older model preset seeded, still saved on a printer
/// whose configuration names its chamber sensor differently.
constexpr const char* kStaleChamberSensor = "temperature_sensor box";

/// The temperature objects an Elegoo Centauri Carbon on COSMOS reports: one
/// chamber sensor, and neither a chamber heater nor a chamber-named fan.
PrinterDiscovery centauri_carbon() {
    return discovered({"temperature_sensor chamber", "temperature_sensor mcu_toolhead",
                       "temperature_sensor mcu_bed", "temperature_host mainboard",
                       "temperature_fan mainboard", "extruder", "heater_bed"});
}

} // namespace

TEST_CASE("auto takes the chamber sensor discovery picked", "[chamber][assignment]") {
    CHECK(resolve_sensor("auto", centauri_carbon()) == "temperature_sensor chamber");
    CHECK(resolve_sensor("auto", discovered({"temperature_sensor mcu_temp", "extruder"})).empty());
}

TEST_CASE("none disables a chamber sensor discovery found", "[chamber][assignment]") {
    const auto hw = centauri_carbon();
    REQUIRE(hw.has_chamber_sensor());
    CHECK(resolve_sensor("none", hw).empty());
}

TEST_CASE("a named sensor Klipper reports is the chamber sensor", "[chamber][assignment]") {
    SECTION("one no chamber keyword names, so only the assignment can pick it") {
        const auto hw = discovered({"temperature_sensor external_bme", "extruder", "heater_bed"});
        REQUIRE_FALSE(hw.has_chamber_sensor());
        CHECK(resolve_sensor("temperature_sensor external_bme", hw) ==
              "temperature_sensor external_bme");
    }
    SECTION("over the sensor discovery picked") {
        const auto hw =
            discovered({"temperature_sensor chamber", "temperature_sensor external_bme"});
        REQUIRE(hw.chamber_sensor_name() == "temperature_sensor chamber");
        CHECK(resolve_sensor("temperature_sensor external_bme", hw) ==
              "temperature_sensor external_bme");
    }
}

TEST_CASE("a named sensor Klipper does not report is not the chamber sensor",
          "[chamber][assignment]") {
    SECTION("a printer whose chamber sensor has another name reads that sensor") {
        CHECK(resolve_sensor(kStaleChamberSensor, centauri_carbon()) ==
              "temperature_sensor chamber");
    }
    SECTION("a printer with no chamber sensor has none") {
        const auto hw = discovered({"temperature_sensor mcu_temp", "extruder", "heater_bed"});
        REQUIRE_FALSE(hw.has_chamber_sensor());
        CHECK(resolve_sensor(kStaleChamberSensor, hw).empty());
    }
    SECTION("never the chamber heater discovery picked") {
        const auto hw =
            discovered({"heater_generic chamber", "temperature_sensor mcu_temp", "extruder"});
        REQUIRE(hw.chamber_heater_name() == "heater_generic chamber");
        REQUIRE_FALSE(hw.has_chamber_sensor());
        CHECK(resolve_sensor(kStaleChamberSensor, hw).empty());
    }
    SECTION("nothing counts before Klipper has reported its objects") {
        const PrinterDiscovery hw;
        REQUIRE(hw.printer_objects().empty());
        CHECK(resolve_sensor(kStaleChamberSensor, hw).empty());
    }
}
