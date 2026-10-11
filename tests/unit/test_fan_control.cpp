// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fan_gcode.h"

#include <iomanip>
#include <sstream>
#include <vector>

#include "../catch_amalgamated.hpp"

TEST_CASE("Fan gcode generation", "[fan][gcode]") {
    SECTION("bare fan uses M106 S<value>") {
        auto gcode = helix::fan_gcode("fan", 100.0);
        REQUIRE(gcode == "M106 S255");
    }

    SECTION("bare fan at 50% uses M106 S128") {
        auto gcode = helix::fan_gcode("fan", 50.0);
        REQUIRE(gcode == "M106 S128");
    }

    SECTION("bare fan off uses M107") {
        auto gcode = helix::fan_gcode("fan", 0.0);
        REQUIRE(gcode == "M107");
    }

    SECTION("output_pin fan0 uses M106 P0") {
        auto gcode = helix::fan_gcode("output_pin fan0", 100.0);
        REQUIRE(gcode == "M106 P0 S255");
    }

    SECTION("output_pin fan2 at 50% uses M106 P2 S128") {
        auto gcode = helix::fan_gcode("output_pin fan2", 50.0);
        REQUIRE(gcode == "M106 P2 S128");
    }

    SECTION("output_pin fan0 off uses M107 P0") {
        auto gcode = helix::fan_gcode("output_pin fan0", 0.0);
        REQUIRE(gcode == "M107 P0");
    }

    SECTION("output_pin non-fan uses SET_PIN") {
        auto gcode = helix::fan_gcode("output_pin aux_blower", 75.0);
        REQUIRE(gcode == "SET_PIN PIN=aux_blower VALUE=0.75");
    }

    SECTION("fan_generic uses SET_FAN_SPEED") {
        auto gcode = helix::fan_gcode("fan_generic aux_fan", 50.0);
        REQUIRE(gcode == "SET_FAN_SPEED FAN=aux_fan SPEED=0.50");
    }

    SECTION("heater_fan uses SET_FAN_SPEED") {
        auto gcode = helix::fan_gcode("heater_fan hotend_fan", 100.0);
        REQUIRE(gcode == "SET_FAN_SPEED FAN=hotend_fan SPEED=1.00");
    }
}

// fan_gcode's fractional values must be the bytes iostreams' fixed/setprecision(2)
// printed, so a macro or config comparing the command text keeps matching.
TEST_CASE("Fan gcode fractional values match fixed two-decimal formatting", "[fan][gcode]") {
    SECTION("pinned values") {
        CHECK(helix::fan_gcode("output_pin aux", 33.333) == "SET_PIN PIN=aux VALUE=0.33");
        CHECK(helix::fan_gcode("output_pin aux", 66.666) == "SET_PIN PIN=aux VALUE=0.67");
        CHECK(helix::fan_gcode("output_pin aux", 0.4) == "SET_PIN PIN=aux VALUE=0.00");
        CHECK(helix::fan_gcode("output_pin aux", 150.0) == "SET_PIN PIN=aux VALUE=1.50");
        CHECK(helix::fan_gcode("fan_generic aux", -10.0) == "SET_FAN_SPEED FAN=aux SPEED=-0.10");
        CHECK(helix::fan_gcode("fan_generic aux", 1e7) == "SET_FAN_SPEED FAN=aux SPEED=100000.00");
    }

    SECTION("matches an ostringstream reference across the range") {
        auto reference = [](const char* prefix, double speed_percent) {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2) << prefix << (speed_percent / 100.0);
            return ss.str();
        };
        std::vector<double> values = {0.5, 0.49999, 12.345, 99.5, 99.4999, 1e-9, 1e20};
        for (double v = 0.0; v <= 100.0; v += 0.037) {
            values.push_back(v);
        }
        for (double v : values) {
            INFO("speed_percent=" << v);
            CHECK(helix::fan_gcode("output_pin aux", v) == reference("SET_PIN PIN=aux VALUE=", v));
            CHECK(helix::fan_gcode("fan_generic aux", v) ==
                  reference("SET_FAN_SPEED FAN=aux SPEED=", v));
        }
    }
}
