// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_chamber_air_modes.cpp
 * @brief The chamber air circulation mode capability: which printers offer
 *        which modes, and the gcode each mode sends.
 */

#include "chamber_air_modes.h"
#include "printer_discovery.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using helix::PrinterDiscovery;
using nlohmann::json;
namespace air = helix::chamber_air;
using air::AirMode;

namespace {

PrinterDiscovery printer_with(std::initializer_list<const char*> extra) {
    json objects = json::array({"gcode_move", "toolhead", "extruder"});
    for (const char* o : extra) {
        objects.push_back(o);
    }
    PrinterDiscovery hw;
    hw.parse_objects(objects);
    return hw;
}

/// A Creator 5 Pro on Z-Mod with the named air circulation macros.
PrinterDiscovery zmod_c5(std::initializer_list<const char*> macros) {
    json objects = json::array({"gcode_move", "toolhead", "extruder", "zmod", "zmod_color"});
    for (int i = 1; i <= 4; ++i) {
        objects.push_back("gcode_button extruder_pos" + std::to_string(i));
        objects.push_back("gcode_button extruder_grab" + std::to_string(i));
    }
    for (const char* m : macros) {
        objects.push_back(std::string("gcode_macro ") + m);
    }
    PrinterDiscovery hw;
    hw.parse_objects(objects);
    return hw;
}

PrinterDiscovery full_zmod_c5() {
    return zmod_c5(
        {"AIR_CIRCULATION_INTERNAL", "AIR_CIRCULATION_EXTERNAL", "AIR_CIRCULATION_STOP"});
}

} // namespace

TEST_CASE("chamber air: a Z-Mod Creator 5 offers all three modes", "[chamber_air]") {
    const PrinterDiscovery c5 = full_zmod_c5();
    CHECK(air::supported(c5));
    CHECK(air::modes(c5) ==
          std::vector<AirMode>{AirMode::Recirculate, AirMode::Exhaust, AirMode::Off});
    CHECK_FALSE(air::provider_name(c5).empty());
}

TEST_CASE("chamber air: each mode sends its own macro", "[chamber_air]") {
    const PrinterDiscovery c5 = full_zmod_c5();
    CHECK(air::mode_gcode(c5, AirMode::Recirculate) == "AIR_CIRCULATION_INTERNAL");
    CHECK(air::mode_gcode(c5, AirMode::Exhaust) == "AIR_CIRCULATION_EXTERNAL");
    CHECK(air::mode_gcode(c5, AirMode::Off) == "AIR_CIRCULATION_STOP");
}

TEST_CASE("chamber air: a mode whose macro is missing is not offered", "[chamber_air]") {
    const PrinterDiscovery c5 = zmod_c5({"AIR_CIRCULATION_STOP"});
    CHECK(air::supported(c5));
    CHECK(air::modes(c5) == std::vector<AirMode>{AirMode::Off});
    CHECK_FALSE(air::offers(c5, AirMode::Exhaust));
    CHECK(air::mode_gcode(c5, AirMode::Exhaust).empty());
}

TEST_CASE("chamber air: printers without the macros offer nothing", "[chamber_air]") {
    const PrinterDiscovery bare_c5 = zmod_c5({});
    CHECK_FALSE(air::supported(bare_c5));
    CHECK(air::modes(bare_c5).empty());

    // The macros are the capability on any printer, and only the defined
    // ones are offered.
    const PrinterDiscovery other =
        printer_with({"gcode_macro AIR_CIRCULATION_INTERNAL", "gcode_macro AIR_CIRCULATION_STOP"});
    CHECK(air::supported(other));
    CHECK(air::mode_gcode(other, AirMode::Recirculate) == "AIR_CIRCULATION_INTERNAL");
    CHECK(air::mode_gcode(other, AirMode::Exhaust).empty());

    const PrinterDiscovery bare = printer_with({});
    CHECK_FALSE(air::supported(bare));
    CHECK(air::provider_name(bare).empty());
}

TEST_CASE("chamber air: mode ids round-trip and reject anything else", "[chamber_air]") {
    for (AirMode m : {AirMode::Recirculate, AirMode::Exhaust, AirMode::Off}) {
        CHECK(air::parse_mode_id(air::mode_id(m)) == m);
    }
    CHECK(air::parse_mode_id("exhaust") == AirMode::Exhaust);
    CHECK_FALSE(air::parse_mode_id("").has_value());
    CHECK_FALSE(air::parse_mode_id("Exhaust").has_value());
}
