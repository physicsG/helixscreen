// tests/unit/test_chamber_heater_discovery.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "printer_discovery.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using namespace helix;

namespace {
PrinterDiscovery parse(std::initializer_list<const char*> objs) {
    PrinterDiscovery d;
    nlohmann::json arr = nlohmann::json::array();
    for (const char* o : objs)
        arr.push_back(o);
    d.parse_objects(arr);
    return d;
}
} // namespace

TEST_CASE("dragonbreath heater auto-detects with diagnostics", "[chamber][discovery]") {
    auto d = parse({"heater_bed", "extruder", "heater_generic dragonbreath", "dragonbreath",
                    "output_pin dragonbreath_filter"});
    CHECK(d.has_chamber_heater());
    CHECK(d.chamber_heater_name() == "heater_generic dragonbreath");
    CHECK(d.chamber_heater_backend_id() == "dragonbreath");
    CHECK(d.chamber_diagnostics_object() == "dragonbreath");
    CHECK(d.chamber_filter_fan_pin() == "output_pin dragonbreath_filter");
}

TEST_CASE("printer-native chamber wins over appliance name", "[chamber][discovery]") {
    auto d = parse({"heater_generic chamber", "heater_generic dragonbreath", "dragonbreath"});
    CHECK(d.chamber_heater_name() == "heater_generic chamber");
    CHECK(d.chamber_heater_backend_id() == "generic");
    CHECK(d.chamber_diagnostics_object().empty());
}

TEST_CASE("panda_breath heater detects, no diagnostics", "[chamber][discovery]") {
    auto d = parse({"heater_generic panda_breath"});
    CHECK(d.chamber_heater_name() == "heater_generic panda_breath");
    CHECK(d.chamber_heater_backend_id() == "panda_breath");
    CHECK(d.chamber_diagnostics_object().empty());
    CHECK(d.chamber_filter_fan_pin().empty());
}

TEST_CASE("existing keyword behavior unchanged", "[chamber][discovery]") {
    auto d = parse({"heater_generic chamber_heater", "temperature_fan chamber_fan"});
    CHECK(d.chamber_heater_name() == "heater_generic chamber_heater");
    CHECK(d.chamber_heater_backend_id() == "generic");
    CHECK(d.chamber_cooling_fan_name() == "temperature_fan chamber_fan"); // K2 style intact
    auto none = parse({"heater_bed", "extruder"});
    CHECK_FALSE(none.has_chamber_heater());
    CHECK(none.chamber_heater_backend_id().empty());
}
