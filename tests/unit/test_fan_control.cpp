// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fan_gcode.h"

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
