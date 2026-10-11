// tests/unit/test_chamber_heater_discovery.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "printer_discovery.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using namespace helix;

namespace {
PrinterDiscovery parse(std::initializer_list<std::string> objs) {
    PrinterDiscovery d;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& o : objs)
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

TEST_CASE("panda_breath heater detects with diagnostics, no filter pin", "[chamber][discovery]") {
    auto d = parse({"heater_generic panda_breath", "panda_breath"});
    CHECK(d.chamber_heater_name() == "heater_generic panda_breath");
    CHECK(d.chamber_heater_backend_id() == "panda_breath");
    CHECK(d.chamber_diagnostics_object() == "panda_breath");
    // The stock binding publishes no filtration pin: the appliance runs its
    // filter from its own auto settings.
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

namespace {
// Object names spanning the keyword tiers, both penalties, case handling and
// the separator rules (whitespace splits tokens, hyphen does not).
const std::vector<std::string> kKeywordNames = {
    "chamber",          // 100 exact keyword
    "ChAmBeR",          // 100 case-insensitive
    "chamber_temp",     // 99 compound penalty
    "my chamber",       // 99 whitespace-separated compound
    "chamber-tvoc",     // 99 hyphen is not a separator: no air-quality penalty
    "enclosure",        // 90
    "enclosure_tvoc",   // 49 air-quality penalty
    "cavity",           // 85
    "cavity_pressure",  // 44
    "box",              // 60 standalone token
    "box_fan",          // 59
    "box_gas",          // 19
    "chamber_humidity", // 59
};

// Which of the two candidate object names a recorded full name kept.
std::string picked_name(const std::string& full, const std::string& prefix,
                        const std::string& first, const std::string& second) {
    return full == prefix + first ? first : second;
}
} // namespace

TEST_CASE("all three chamber paths rank the same names identically", "[chamber][discovery]") {
    for (const auto& first : kKeywordNames) {
        for (const auto& second : kKeywordNames) {
            if (first == second) {
                continue;
            }
            auto heater = parse({"heater_generic " + first, "heater_generic " + second});
            auto sensor = parse({"temperature_sensor " + first, "temperature_sensor " + second});
            auto fan = parse({"temperature_fan " + first, "temperature_fan " + second});
            // Every path scores the object NAME with the same keyword rule, so
            // the winner must be path-independent — including on equal scores,
            // where all three keep whichever object was listed first.
            std::string heater_pick =
                picked_name(heater.chamber_heater_name(), "heater_generic ", first, second);
            std::string sensor_pick =
                picked_name(sensor.chamber_sensor_name(), "temperature_sensor ", first, second);
            std::string fan_pick =
                picked_name(fan.chamber_cooling_fan_name(), "temperature_fan ", first, second);
            INFO("first=" << first << " second=" << second);
            CHECK(heater_pick == sensor_pick);
            CHECK(heater_pick == fan_pick);
        }
    }
}

TEST_CASE("sensor and cooling-fan paths score keywords only", "[chamber][discovery]") {
    // No chamber keyword: never a sensor or cooling-fan candidate.
    auto numbered_box = parse({"temperature_sensor box1_heater"});
    CHECK_FALSE(numbered_box.has_chamber_sensor());
    CHECK(numbered_box.chamber_sensor_name().empty());
    CHECK(parse({"temperature_fan boxx"}).chamber_cooling_fan_name().empty());

    // Appliance names are match()-only (heater slot). The sensor and
    // cooling-fan slots are decided by keyword score alone, so an appliance
    // name never claims them even though match() gives it 95.
    CHECK_FALSE(parse({"temperature_sensor dragonbreath"}).has_chamber_sensor());
    CHECK(parse({"temperature_fan panda_breath"}).chamber_cooling_fan_name().empty());

    // A floored air-quality chamber sensor stays detectable.
    CHECK(parse({"temperature_sensor chamber_humidity"}).has_chamber_sensor());
}

TEST_CASE("a chamber heater leaves no separate chamber sensor", "[chamber][discovery]") {
    // A heater carries its own temperature, so it is the chamber reading and
    // the sensor role stays empty. A keyword-matched probe alongside it is the
    // printer's own sensor, free to be listed and assigned on its own.
    auto u1 = parse({"heater_generic panda_breath", "panda_breath", "temperature_sensor cavity"});
    CHECK(u1.chamber_heater_name() == "heater_generic panda_breath");
    CHECK_FALSE(u1.has_chamber_sensor());
    CHECK(u1.chamber_sensor_name().empty());

    // Same rule for an integrated chamber: a chamber-named probe at keyword
    // 100 still yields to the object that actually heats.
    auto integrated = parse({"heater_generic chamber_heater", "temperature_sensor chamber_temp"});
    CHECK(integrated.chamber_heater_name() == "heater_generic chamber_heater");
    CHECK_FALSE(integrated.has_chamber_sensor());

    // A chamber-named temperature_fan is both the heater and its own reading.
    auto fan_driven = parse({"temperature_fan chamber_fan", "temperature_sensor cavity"});
    CHECK(fan_driven.chamber_heater_name() == "temperature_fan chamber_fan");
    CHECK_FALSE(fan_driven.has_chamber_sensor());
}

TEST_CASE("without a chamber heater the sensor heuristics still decide", "[chamber][discovery]") {
    auto d = parse({"extruder", "heater_bed", "temperature_sensor cavity"});
    CHECK_FALSE(d.has_chamber_heater());
    CHECK(d.has_chamber_sensor());
    CHECK(d.chamber_sensor_name() == "temperature_sensor cavity");

    // A stronger keyword still wins among probes when nothing heats.
    auto ranked = parse({"temperature_sensor cavity", "temperature_sensor chamber"});
    CHECK(ranked.chamber_sensor_name() == "temperature_sensor chamber");
}
