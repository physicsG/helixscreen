// tests/unit/test_power_loss_sensor.cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "power_loss_sensor.h"
#include "printer_discovery.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using helix::PrinterDiscovery;
using namespace helix::power_loss;

namespace {

PrinterDiscovery hardware_with_objects(const std::vector<std::string>& names) {
    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array();
    for (const auto& n : names) {
        objects.push_back(n);
    }
    hw.parse_objects(objects);
    return hw;
}

} // namespace

TEST_CASE("power loss sensor: detected by its object", "[power_loss][sensor]") {
    PrinterDiscovery hw = hardware_with_objects({"power_loss_check"});
    REQUIRE(firmware_reports_power_loss(hw));
}

TEST_CASE("power loss sensor: only the bare name detects", "[power_loss][sensor]") {
    // The firmware also publishes power_loss_check e0..e3, one per extruder,
    // all uninitialised. Detection on a prefix or substring would match those
    // and read a monitor that has never taken a reading.
    PrinterDiscovery hw = hardware_with_objects({"power_loss_check e0", "power_loss_check e1",
                                                 "power_loss_check e2", "power_loss_check e3"});
    REQUIRE_FALSE(firmware_reports_power_loss(hw));
    REQUIRE(required_status_objects(hw).empty());
}

TEST_CASE("power loss sensor: the flag reads through", "[power_loss][sensor]") {
    PrinterDiscovery hw = hardware_with_objects({"power_loss_check"});
    nlohmann::json s;
    s["power_loss_check"] = {{"initialized", 1}, {"power_loss_flag", 1}};
    REQUIRE(power_loss_asserted(hw, s).value() == true);
    s["power_loss_check"]["power_loss_flag"] = 0;
    REQUIRE(power_loss_asserted(hw, s).value() == false);
}

TEST_CASE("power loss sensor: an uninitialised sensor answers nothing", "[power_loss][sensor]") {
    // initialized:0 means the monitor has not taken a reading. Reading its flag
    // as a false would report "mains is fine" on no evidence.
    PrinterDiscovery hw = hardware_with_objects({"power_loss_check"});
    nlohmann::json s;
    s["power_loss_check"] = {{"initialized", 0}, {"power_loss_flag", 0}};
    REQUIRE_FALSE(power_loss_asserted(hw, s).has_value());
}

TEST_CASE("power loss sensor: a frame that omits the object answers nothing",
          "[power_loss][sensor]") {
    // Moonraker sends delta frames; an object absent from one is silence, not
    // a report that mains is fine.
    PrinterDiscovery hw = hardware_with_objects({"power_loss_check"});
    REQUIRE_FALSE(power_loss_asserted(hw, nlohmann::json::object()).has_value());
}

TEST_CASE("power loss sensor: a printer without the sensor answers nothing",
          "[power_loss][sensor]") {
    PrinterDiscovery hw = hardware_with_objects({"bed_mesh", "quad_gantry_level"});
    nlohmann::json s;
    s["power_loss_check"] = {{"initialized", 1}, {"power_loss_flag", 1}};
    REQUIRE_FALSE(power_loss_asserted(hw, s).has_value());
    REQUIRE(required_status_objects(hw).empty());
}
