// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hh_defaults.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::printer;

// ============================================================================
// Section Tests
// ============================================================================

TEST_CASE("HH default sections: count", "[ams][hh_defaults]") {
    auto sections = hh_default_sections();
    REQUIRE(sections.size() == 5);
}

TEST_CASE("HH default sections: required fields", "[ams][hh_defaults]") {
    for (const auto& s : hh_default_sections()) {
        REQUIRE(!s.id.empty());
        REQUIRE(!s.label.empty());
        REQUIRE(s.display_order >= 0);
        REQUIRE(!s.description.empty());
    }
}

TEST_CASE("HH default sections: known IDs", "[ams][hh_defaults]") {
    auto sections = hh_default_sections();
    std::set<std::string> ids;
    for (const auto& s : sections) {
        ids.insert(s.id);
    }
    REQUIRE(ids.count("setup") == 1);
    REQUIRE(ids.count("speed") == 1);
    REQUIRE(ids.count("toolhead") == 1);
    REQUIRE(ids.count("accessories") == 1);
    REQUIRE(ids.count("maintenance") == 1);
}

TEST_CASE("HH default sections: display order", "[ams][hh_defaults]") {
    auto sections = hh_default_sections();
    for (size_t i = 1; i < sections.size(); i++) {
        REQUIRE(sections[i].display_order > sections[i - 1].display_order);
    }
}

TEST_CASE("HH default sections: unique IDs", "[ams][hh_defaults]") {
    auto sections = hh_default_sections();
    std::set<std::string> ids;
    for (const auto& s : sections) {
        ids.insert(s.id);
    }
    REQUIRE(ids.size() == sections.size());
}

// ============================================================================
// Action Tests
// ============================================================================

TEST_CASE("HH default actions: count", "[ams][hh_defaults]") {
    auto actions = hh_default_actions();
    REQUIRE(actions.size() == 32);
}

TEST_CASE("HH default actions: required fields", "[ams][hh_defaults]") {
    for (const auto& a : hh_default_actions()) {
        REQUIRE(!a.id.empty());
        REQUIRE(!a.label.empty());
        REQUIRE(!a.section.empty());
    }
}

TEST_CASE("HH default actions: unique IDs", "[ams][hh_defaults]") {
    auto actions = hh_default_actions();
    std::set<std::string> ids;
    for (const auto& a : actions) {
        ids.insert(a.id);
    }
    REQUIRE(ids.size() == actions.size());
}

TEST_CASE("HH default actions: known IDs", "[ams][hh_defaults]") {
    auto actions = hh_default_actions();
    std::set<std::string> ids;
    for (const auto& a : actions) {
        ids.insert(a.id);
    }
    // Setup
    REQUIRE(ids.count("calibrate_bowden") == 1);
    REQUIRE(ids.count("calibrate_encoder") == 1);
    REQUIRE(ids.count("calibrate_gear") == 1);
    REQUIRE(ids.count("calibrate_gates") == 1);
    REQUIRE(ids.count("led_mode") == 1);
    // Speed
    REQUIRE(ids.count("gear_from_buffer_speed") == 1);
    REQUIRE(ids.count("gear_from_spool_speed") == 1);
    REQUIRE(ids.count("gear_unload_speed") == 1);
    REQUIRE(ids.count("selector_speed") == 1);
    REQUIRE(ids.count("extruder_load_speed") == 1);
    REQUIRE(ids.count("extruder_unload_speed") == 1);
    // Toolhead
    REQUIRE(ids.count("toolhead_sensor_to_nozzle") == 1);
    REQUIRE(ids.count("toolhead_extruder_to_nozzle") == 1);
    REQUIRE(ids.count("toolhead_entry_to_extruder") == 1);
    REQUIRE(ids.count("toolhead_ooze_reduction") == 1);
    // Accessories
    REQUIRE(ids.count("espooler_mode") == 1);
    REQUIRE(ids.count("clog_detection") == 1);
    REQUIRE(ids.count("sync_to_extruder") == 1);
    // Maintenance
    REQUIRE(ids.count("test_grip") == 1);
    REQUIRE(ids.count("test_load") == 1);
    REQUIRE(ids.count("test_move") == 1);
    REQUIRE(ids.count("motors_toggle") == 1);
    REQUIRE(ids.count("servo_buzz") == 1);
    REQUIRE(ids.count("reset_servo_counter") == 1);
    REQUIRE(ids.count("reset_blade_counter") == 1);
    REQUIRE(ids.count("load_extruder") == 1);
    REQUIRE(ids.count("unload_extruder") == 1);
    REQUIRE(ids.count("spoolman_refresh") == 1);
}

TEST_CASE("HH default actions: extruder load and unload share the first button row",
          "[ams][hh_defaults]") {
    // The section overlay packs consecutive buttons two to a row.
    std::vector<std::string> maintenance_buttons;
    for (const auto& a : hh_default_actions()) {
        if (a.section == "maintenance" && a.type == ActionType::BUTTON) {
            maintenance_buttons.push_back(a.id);
        }
    }
    REQUIRE(maintenance_buttons.size() >= 2);
    CHECK(maintenance_buttons[0] == "load_extruder");
    CHECK(maintenance_buttons[1] == "unload_extruder");
}

TEST_CASE("HH default actions: section assignments", "[ams][hh_defaults]") {
    auto actions = hh_default_actions();
    auto sections = hh_default_sections();

    std::set<std::string> valid_sections;
    for (const auto& s : sections) {
        valid_sections.insert(s.id);
    }

    for (const auto& a : actions) {
        REQUIRE(valid_sections.count(a.section) == 1);
    }
}

TEST_CASE("HH default actions: slider ranges valid", "[ams][hh_defaults]") {
    for (const auto& a : hh_default_actions()) {
        if (a.type == ActionType::SLIDER) {
            REQUIRE(a.min_value < a.max_value);
            REQUIRE(!a.unit.empty());
        }
    }
}

TEST_CASE("HH default actions: dropdown has options", "[ams][hh_defaults]") {
    for (const auto& a : hh_default_actions()) {
        if (a.type == ActionType::DROPDOWN) {
            REQUIRE(a.options.size() >= 2);
        }
    }
}
