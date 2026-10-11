// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_probe_sensor_manager.cpp
 * @brief Unit tests for ProbeSensorManager
 *
 * Tests cover:
 * - Type helpers (role/type string conversion)
 * - Sensor discovery from Klipper object names (probe, bltouch, smart_effector, probe_eddy_current)
 * - Role assignment (Z_PROBE)
 * - State updates from Moonraker status JSON
 * - Subject value correctness for UI binding
 * - Config persistence
 */

#include "../test_helpers/update_queue_test_access.h"
#include "../ui_test_utils.h"
#include "config.h"
#include "probe_sensor_manager.h"
#include "probe_sensor_types.h"

#include <spdlog/spdlog.h>

#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::sensors;
using json = nlohmann::json;

// ============================================================================
// Test Access
// ============================================================================

namespace helix::sensors {
class ProbeSensorManagerTestAccess {
  public:
    static void reset(ProbeSensorManager& obj) {
        std::lock_guard<std::recursive_mutex> lock(obj.mutex_);
        obj.sensors_.clear();
        obj.sync_mode_ = true;
        // Reset subject values but keep subjects initialized
        if (obj.subjects_initialized_) {
            lv_subject_set_int(&obj.sensor_count_, 0);
            lv_subject_set_int(&obj.probe_triggered_, -1);
            lv_subject_set_int(&obj.probe_last_z_, -1);
            lv_subject_set_int(&obj.probe_z_offset_, -1);
        }
    }
};
} // namespace helix::sensors

// ============================================================================
// Test Fixture
// ============================================================================

class ProbeSensorTestFixture {
  public:
    ProbeSensorTestFixture() {
        // Initialize LVGL (safe version avoids "already initialized" warnings)
        lv_init_safe();

        // Create a headless display for testing
        if (!display_created_) {
            display_ = lv_display_create(480, 320);
            alignas(64) static lv_color_t buf[480 * 10];
            lv_display_set_buffers(display_, buf, nullptr, sizeof(buf),
                                   LV_DISPLAY_RENDER_MODE_PARTIAL);
            lv_display_set_flush_cb(display_,
                                    [](lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
                                        lv_display_flush_ready(disp);
                                    });
            display_created_ = true;
        }

        // Initialize subjects (idempotent)
        mgr().init_subjects();

        // Reset state for test isolation
        ProbeSensorManagerTestAccess::reset(mgr());
    }

    ~ProbeSensorTestFixture() {
        // Reset after each test
        ProbeSensorManagerTestAccess::reset(mgr());
    }

  protected:
    ProbeSensorManager& mgr() {
        return ProbeSensorManager::instance();
    }

    // Helper to discover standard test sensors
    // Uses two specific (non-STANDARD) types to avoid deduplication logic
    // that removes generic "probe" entries when specific types exist.
    void discover_test_sensors() {
        std::vector<std::string> sensors = {"bltouch", "smart_effector"};
        mgr().discover(sensors);
    }

    // Helper to simulate Moonraker status update
    void update_sensor_state(const std::string& klipper_name, float last_z_result) {
        json status;
        status[klipper_name]["last_z_result"] = last_z_result;
        mgr().update_from_status(status);
    }

  private:
    static lv_display_t* display_;
    static bool display_created_;
};

// Static members
lv_display_t* ProbeSensorTestFixture::display_ = nullptr;
bool ProbeSensorTestFixture::display_created_ = false;

// ============================================================================
// Type Helper Tests (probe_sensor_types.h)
// ============================================================================

TEST_CASE("ProbeSensorTypes - role string conversion", "[probe][types]") {
    SECTION("probe_role_to_string") {
        REQUIRE(probe_role_to_string(ProbeSensorRole::NONE) == "none");
        REQUIRE(probe_role_to_string(ProbeSensorRole::Z_PROBE) == "z_probe");
    }

    SECTION("probe_role_from_string") {
        REQUIRE(probe_role_from_string("none") == ProbeSensorRole::NONE);
        REQUIRE(probe_role_from_string("z_probe") == ProbeSensorRole::Z_PROBE);
        REQUIRE(probe_role_from_string("invalid") == ProbeSensorRole::NONE);
        REQUIRE(probe_role_from_string("") == ProbeSensorRole::NONE);
    }

    SECTION("probe_role_to_display_string") {
        REQUIRE(probe_role_to_display_string(ProbeSensorRole::NONE) == "Unassigned");
        REQUIRE(probe_role_to_display_string(ProbeSensorRole::Z_PROBE) == "Z Probe");
    }
}

TEST_CASE("ProbeSensorTypes - type string conversion", "[probe][types]") {
    SECTION("probe_type_to_string") {
        REQUIRE(probe_type_to_string(ProbeSensorType::STANDARD) == "standard");
        REQUIRE(probe_type_to_string(ProbeSensorType::BLTOUCH) == "bltouch");
        REQUIRE(probe_type_to_string(ProbeSensorType::SMART_EFFECTOR) == "smart_effector");
        REQUIRE(probe_type_to_string(ProbeSensorType::EDDY_CURRENT) == "eddy_current");
    }

    SECTION("probe_type_from_string") {
        REQUIRE(probe_type_from_string("standard") == ProbeSensorType::STANDARD);
        REQUIRE(probe_type_from_string("bltouch") == ProbeSensorType::BLTOUCH);
        REQUIRE(probe_type_from_string("smart_effector") == ProbeSensorType::SMART_EFFECTOR);
        REQUIRE(probe_type_from_string("eddy_current") == ProbeSensorType::EDDY_CURRENT);
        REQUIRE(probe_type_from_string("invalid") == ProbeSensorType::STANDARD);
        REQUIRE(probe_type_from_string("") == ProbeSensorType::STANDARD);
    }
}

TEST_CASE("Probe type string conversions - new types", "[probe][types]") {
    SECTION("cartographer type to string") {
        REQUIRE(probe_type_to_string(ProbeSensorType::CARTOGRAPHER) == "cartographer");
    }
    SECTION("beacon type to string") {
        REQUIRE(probe_type_to_string(ProbeSensorType::BEACON) == "beacon");
    }
    SECTION("tap type to string") {
        REQUIRE(probe_type_to_string(ProbeSensorType::TAP) == "tap");
    }
    SECTION("klicky type to string") {
        REQUIRE(probe_type_to_string(ProbeSensorType::KLICKY) == "klicky");
    }
    SECTION("cartographer from string") {
        REQUIRE(probe_type_from_string("cartographer") == ProbeSensorType::CARTOGRAPHER);
    }
    SECTION("beacon from string") {
        REQUIRE(probe_type_from_string("beacon") == ProbeSensorType::BEACON);
    }
    SECTION("tap from string") {
        REQUIRE(probe_type_from_string("tap") == ProbeSensorType::TAP);
    }
    SECTION("klicky from string") {
        REQUIRE(probe_type_from_string("klicky") == ProbeSensorType::KLICKY);
    }
    SECTION("loadcell type to string") {
        REQUIRE(probe_type_to_string(ProbeSensorType::LOADCELL) == "loadcell");
    }
    SECTION("loadcell from string") {
        REQUIRE(probe_type_from_string("loadcell") == ProbeSensorType::LOADCELL);
    }
    SECTION("loadcell display string") {
        REQUIRE(probe_type_to_display_string(ProbeSensorType::LOADCELL) == "Load Cell");
    }
}

// ============================================================================
// Sensor Discovery Tests
// ============================================================================

TEST_CASE_METHOD(ProbeSensorTestFixture, "ProbeSensorManager - discovery", "[probe][discovery]") {
    SECTION("Discovers standard probe") {
        std::vector<std::string> sensors = {"probe"};
        mgr().discover(sensors);

        REQUIRE(mgr().has_sensors());
        REQUIRE(mgr().sensor_count() == 1);

        auto configs = mgr().get_sensors();
        REQUIRE(configs.size() == 1);
        REQUIRE(configs[0].klipper_name == "probe");
        REQUIRE(configs[0].sensor_name == "probe");
        REQUIRE(configs[0].type == ProbeSensorType::STANDARD);
        REQUIRE(configs[0].enabled == true);
        // Role starts as NONE — auto-assign happens in load_config_from_file()
        REQUIRE(configs[0].role == ProbeSensorRole::NONE);
    }

    SECTION("Discovers BLTouch probe") {
        std::vector<std::string> sensors = {"bltouch"};
        mgr().discover(sensors);

        REQUIRE(mgr().sensor_count() == 1);

        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].klipper_name == "bltouch");
        REQUIRE(configs[0].sensor_name == "bltouch");
        REQUIRE(configs[0].type == ProbeSensorType::BLTOUCH);
    }

    SECTION("Discovers Smart Effector probe") {
        std::vector<std::string> sensors = {"smart_effector"};
        mgr().discover(sensors);

        REQUIRE(mgr().sensor_count() == 1);

        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].klipper_name == "smart_effector");
        REQUIRE(configs[0].sensor_name == "smart_effector");
        REQUIRE(configs[0].type == ProbeSensorType::SMART_EFFECTOR);
    }

    SECTION("Discovers Eddy Current probe with name parameter") {
        std::vector<std::string> sensors = {"probe_eddy_current btt"};
        mgr().discover(sensors);

        REQUIRE(mgr().sensor_count() == 1);

        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].klipper_name == "probe_eddy_current btt");
        REQUIRE(configs[0].sensor_name == "btt");
        REQUIRE(configs[0].type == ProbeSensorType::EDDY_CURRENT);
    }

    SECTION("Discovers multiple probe types") {
        std::vector<std::string> sensors = {"bltouch", "smart_effector",
                                            "probe_eddy_current scanner"};
        mgr().discover(sensors);

        REQUIRE(mgr().sensor_count() == 3);

        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].type == ProbeSensorType::BLTOUCH);
        REQUIRE(configs[1].type == ProbeSensorType::SMART_EFFECTOR);
        REQUIRE(configs[2].type == ProbeSensorType::EDDY_CURRENT);
        REQUIRE(configs[2].sensor_name == "scanner");
    }

    SECTION("Standard probe removed when specific type present") {
        std::vector<std::string> sensors = {"probe", "bltouch"};
        mgr().discover(sensors);

        REQUIRE(mgr().sensor_count() == 1);
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].type == ProbeSensorType::BLTOUCH);
    }

    SECTION("Ignores unrelated objects") {
        std::vector<std::string> sensors = {"probe", "filament_switch_sensor runout",
                                            "temperature_sensor chamber", "extruder"};
        mgr().discover(sensors);

        REQUIRE(mgr().sensor_count() == 1);
        REQUIRE(mgr().get_sensors()[0].klipper_name == "probe");
    }

    SECTION("Empty sensor list clears previous sensors") {
        discover_test_sensors();
        REQUIRE(mgr().sensor_count() == 2);

        mgr().discover({});
        REQUIRE(mgr().sensor_count() == 0);
        REQUIRE_FALSE(mgr().has_sensors());
    }

    SECTION("Re-discovery replaces sensor list") {
        std::vector<std::string> sensors1 = {"probe"};
        mgr().discover(sensors1);
        REQUIRE(mgr().get_sensors()[0].klipper_name == "probe");

        std::vector<std::string> sensors2 = {"bltouch"};
        mgr().discover(sensors2);
        REQUIRE(mgr().sensor_count() == 1);
        REQUIRE(mgr().get_sensors()[0].klipper_name == "bltouch");
    }

    SECTION("Sensor count subject is updated") {
        lv_subject_t* count_subject = mgr().get_sensor_count_subject();
        REQUIRE(lv_subject_get_int(count_subject) == 0);

        discover_test_sensors();
        REQUIRE(lv_subject_get_int(count_subject) == 2);

        mgr().discover({});
        REQUIRE(lv_subject_get_int(count_subject) == 0);
    }
}

// ============================================================================
// Role Assignment Tests
// ============================================================================

TEST_CASE_METHOD(ProbeSensorTestFixture, "ProbeSensorManager - role assignment", "[probe][roles]") {
    discover_test_sensors();

    SECTION("Can set Z_PROBE role") {
        mgr().set_sensor_role("bltouch", ProbeSensorRole::Z_PROBE);

        auto configs = mgr().get_sensors();
        auto it = std::find_if(configs.begin(), configs.end(),
                               [](const auto& c) { return c.klipper_name == "bltouch"; });
        REQUIRE(it != configs.end());
        REQUIRE(it->role == ProbeSensorRole::Z_PROBE);
    }

    SECTION("Role assignment is unique - assigning same role clears previous") {
        mgr().set_sensor_role("bltouch", ProbeSensorRole::Z_PROBE);
        mgr().set_sensor_role("smart_effector", ProbeSensorRole::Z_PROBE);

        auto configs = mgr().get_sensors();

        auto bltouch_it = std::find_if(configs.begin(), configs.end(),
                                       [](const auto& c) { return c.klipper_name == "bltouch"; });
        REQUIRE(bltouch_it->role == ProbeSensorRole::NONE);

        auto smart_it = std::find_if(configs.begin(), configs.end(), [](const auto& c) {
            return c.klipper_name == "smart_effector";
        });
        REQUIRE(smart_it->role == ProbeSensorRole::Z_PROBE);
    }

    SECTION("Can assign NONE without affecting other sensors") {
        mgr().set_sensor_role("bltouch", ProbeSensorRole::Z_PROBE);

        mgr().set_sensor_role("bltouch", ProbeSensorRole::NONE);

        auto configs = mgr().get_sensors();
        auto it = std::find_if(configs.begin(), configs.end(),
                               [](const auto& c) { return c.klipper_name == "bltouch"; });
        REQUIRE(it->role == ProbeSensorRole::NONE);
    }

    SECTION("Assigning role to unknown sensor does nothing") {
        mgr().set_sensor_role("nonexistent_sensor", ProbeSensorRole::Z_PROBE);

        for (const auto& config : mgr().get_sensors()) {
            REQUIRE(config.role == ProbeSensorRole::NONE);
        }
    }
}

// ============================================================================
// State Update Tests
// ============================================================================

TEST_CASE_METHOD(ProbeSensorTestFixture, "ProbeSensorManager - state updates", "[probe][state]") {
    discover_test_sensors();
    mgr().set_sensor_role("bltouch", ProbeSensorRole::Z_PROBE);

    SECTION("Parses last_z_result and last_query from status JSON") {
        auto state = mgr().get_sensor_state(ProbeSensorRole::Z_PROBE);
        REQUIRE(state.has_value());
        REQUIRE(state->last_z_result == 0.0f);
        REQUIRE_FALSE(state->triggered);

        json status;
        status["bltouch"]["last_z_result"] = 0.125f;
        status["bltouch"]["last_query"] = true;
        mgr().update_from_status(status);

        state = mgr().get_sensor_state(ProbeSensorRole::Z_PROBE);
        REQUIRE(state->last_z_result == Catch::Approx(0.125f));
        REQUIRE(state->triggered);
    }

    SECTION("last_query accepts the integer form Cartographer publishes") {
        json status;
        status["bltouch"]["last_query"] = 1;
        mgr().update_from_status(status);
        REQUIRE(mgr().get_sensor_state(ProbeSensorRole::Z_PROBE)->triggered);

        status["bltouch"]["last_query"] = 0;
        mgr().update_from_status(status);
        REQUIRE_FALSE(mgr().get_sensor_state(ProbeSensorRole::Z_PROBE)->triggered);
    }

    SECTION("A null or absent last_query keeps the previous value") {
        json status;
        status["bltouch"]["last_query"] = true;
        mgr().update_from_status(status);

        json null_status;
        null_status["bltouch"]["last_query"] = nullptr;
        mgr().update_from_status(null_status);
        REQUIRE(mgr().get_sensor_state(ProbeSensorRole::Z_PROBE)->triggered);

        json absent;
        absent["bltouch"]["last_z_result"] = 0.2f;
        mgr().update_from_status(absent);
        REQUIRE(mgr().get_sensor_state(ProbeSensorRole::Z_PROBE)->triggered);
    }

    SECTION("A numeric z_offset updates the seed, a null one keeps it") {
        json config;
        config["bltouch"] = {{"z_offset", "-1.850"}};
        mgr().discover_from_config(config);

        json null_status;
        null_status["bltouch"]["z_offset"] = nullptr;
        mgr().update_from_status(null_status);
        REQUIRE(mgr().get_sensor_state(ProbeSensorRole::Z_PROBE)->z_offset ==
                Catch::Approx(-1.850f));

        // Creality K1's Z_OFFSET_APPLY_PROBE moves it live, without a restart.
        json status;
        status["bltouch"]["z_offset"] = -0.5f;
        mgr().update_from_status(status);
        REQUIRE(mgr().get_sensor_state(ProbeSensorRole::Z_PROBE)->z_offset == Catch::Approx(-0.5f));
        REQUIRE(lv_subject_get_int(mgr().get_probe_z_offset_subject()) == -500);
    }

    SECTION("Status update for unknown sensor is ignored") {
        json status;
        status["unknown_sensor"]["last_z_result"] = 0.125f;
        mgr().update_from_status(status);

        REQUIRE(mgr().sensor_count() == 2);
    }

    SECTION("Empty status update is handled") {
        json status = json::object();
        mgr().update_from_status(status);

        REQUIRE(mgr().has_sensors());
    }
}

// ============================================================================
// Subject Value Tests
// ============================================================================

TEST_CASE_METHOD(ProbeSensorTestFixture, "ProbeSensorManager - subject values",
                 "[probe][subjects]") {
    discover_test_sensors();

    SECTION("Probe triggered subject shows -1 when no sensor assigned to role") {
        REQUIRE(lv_subject_get_int(mgr().get_probe_triggered_subject()) == -1);
    }

    SECTION("Last Z result subject shows -1 when no sensor assigned to role") {
        REQUIRE(lv_subject_get_int(mgr().get_probe_last_z_subject()) == -1);
    }

    SECTION("Z offset subject shows -1 when no sensor assigned") {
        REQUIRE(lv_subject_get_int(mgr().get_probe_z_offset_subject()) == -1);
    }

    SECTION("Last Z result subject updates correctly (value x 1000 = microns)") {
        mgr().set_sensor_role("bltouch", ProbeSensorRole::Z_PROBE);

        // After assignment, should show 0 since state defaults to 0.0
        REQUIRE(lv_subject_get_int(mgr().get_probe_last_z_subject()) == 0);

        // Update state with last_z_result = 0.125mm = 125 microns
        update_sensor_state("bltouch", 0.125f);
        REQUIRE(lv_subject_get_int(mgr().get_probe_last_z_subject()) == 125);

        // Update with different value
        update_sensor_state("bltouch", 0.250f);
        REQUIRE(lv_subject_get_int(mgr().get_probe_last_z_subject()) == 250);
    }

    SECTION("Probe triggered subject follows last_query (QUERY_PROBE result)") {
        mgr().set_sensor_role("bltouch", ProbeSensorRole::Z_PROBE);
        REQUIRE(lv_subject_get_int(mgr().get_probe_triggered_subject()) == 0);

        json status;
        status["bltouch"]["last_query"] = true;
        mgr().update_from_status(status);
        REQUIRE(lv_subject_get_int(mgr().get_probe_triggered_subject()) == 1);

        status["bltouch"]["last_query"] = false;
        mgr().update_from_status(status);
        REQUIRE(lv_subject_get_int(mgr().get_probe_triggered_subject()) == 0);
    }

    SECTION("Subjects show -1 when sensor disabled") {
        mgr().set_sensor_role("bltouch", ProbeSensorRole::Z_PROBE);
        update_sensor_state("bltouch", 0.125f);

        mgr().set_sensor_enabled("bltouch", false);
        REQUIRE(lv_subject_get_int(mgr().get_probe_triggered_subject()) == -1);
        REQUIRE(lv_subject_get_int(mgr().get_probe_last_z_subject()) == -1);
        REQUIRE(lv_subject_get_int(mgr().get_probe_z_offset_subject()) == -1);
    }
}

// ============================================================================
// Config Persistence Tests
// ============================================================================

TEST_CASE_METHOD(ProbeSensorTestFixture, "ProbeSensorManager - config persistence",
                 "[probe][config]") {
    discover_test_sensors();

    SECTION("save_config returns JSON with role assignments") {
        mgr().set_sensor_role("bltouch", ProbeSensorRole::Z_PROBE);

        json config = mgr().save_config();

        REQUIRE(config.is_object());
        REQUIRE(config.contains("sensors"));
        REQUIRE(config["sensors"].is_array());
        REQUIRE(config["sensors"].size() == 2);

        bool found_bltouch = false;
        for (const auto& sensor : config["sensors"]) {
            if (sensor["klipper_name"] == "bltouch") {
                REQUIRE(sensor["role"] == "z_probe");
                found_bltouch = true;
            }
        }
        REQUIRE(found_bltouch);
    }

    SECTION("load_config restores role assignments") {
        // Set up config JSON
        json config;
        json sensors_array = json::array();
        json sensor1;
        sensor1["klipper_name"] = "bltouch";
        sensor1["role"] = "z_probe";
        sensor1["enabled"] = true;
        sensors_array.push_back(sensor1);
        config["sensors"] = sensors_array;

        mgr().load_config(config);

        auto configs = mgr().get_sensors();
        auto it = std::find_if(configs.begin(), configs.end(),
                               [](const auto& c) { return c.klipper_name == "bltouch"; });
        REQUIRE(it != configs.end());
        REQUIRE(it->role == ProbeSensorRole::Z_PROBE);
    }

    SECTION("load_config with unknown sensor is handled gracefully") {
        json config;
        json sensors_array = json::array();
        json sensor1;
        sensor1["klipper_name"] = "unknown_sensor";
        sensor1["role"] = "z_probe";
        sensors_array.push_back(sensor1);
        config["sensors"] = sensors_array;

        // Should not crash
        mgr().load_config(config);

        // Existing sensors should be unaffected
        for (const auto& sensor : mgr().get_sensors()) {
            REQUIRE(sensor.role == ProbeSensorRole::NONE);
        }
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE_METHOD(ProbeSensorTestFixture, "ProbeSensorManager - edge cases", "[probe][edge]") {
    SECTION("get_sensor_state returns nullopt for unassigned role") {
        discover_test_sensors();
        auto state = mgr().get_sensor_state(ProbeSensorRole::Z_PROBE);
        REQUIRE_FALSE(state.has_value());
    }

    SECTION("get_sensor_state returns nullopt for NONE role") {
        discover_test_sensors();
        auto state = mgr().get_sensor_state(ProbeSensorRole::NONE);
        REQUIRE_FALSE(state.has_value());
    }

    SECTION("is_sensor_available checks role assignment and enabled") {
        discover_test_sensors();
        REQUIRE_FALSE(mgr().is_sensor_available(ProbeSensorRole::Z_PROBE));

        mgr().set_sensor_role("bltouch", ProbeSensorRole::Z_PROBE);
        REQUIRE(mgr().is_sensor_available(ProbeSensorRole::Z_PROBE));

        mgr().set_sensor_enabled("bltouch", false);
        REQUIRE_FALSE(mgr().is_sensor_available(ProbeSensorRole::Z_PROBE));
    }

    SECTION("Eddy current probe without name parameter is ignored") {
        // "probe_eddy_current" needs a name parameter
        std::vector<std::string> sensors = {"probe_eddy_current"};
        mgr().discover(sensors);

        // Should not discover (needs a name like "probe_eddy_current btt")
        REQUIRE(mgr().sensor_count() == 0);
    }
}

// ============================================================================
// New Probe Type Discovery Tests
// ============================================================================

TEST_CASE_METHOD(ProbeSensorTestFixture, "ProbeSensorManager - discovery of new probe types",
                 "[probe][discovery]") {
    SECTION("Discovers cartographer object") {
        std::vector<std::string> objects = {"cartographer"};
        mgr().discover(objects);
        REQUIRE(mgr().has_sensors());
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].type == ProbeSensorType::CARTOGRAPHER);
        REQUIRE(configs[0].sensor_name == "cartographer");
        REQUIRE(configs[0].klipper_name == "cartographer");
    }

    SECTION("Discovers beacon object") {
        std::vector<std::string> objects = {"beacon"};
        mgr().discover(objects);
        REQUIRE(mgr().has_sensors());
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].type == ProbeSensorType::BEACON);
        REQUIRE(configs[0].sensor_name == "beacon");
        REQUIRE(configs[0].klipper_name == "beacon");
    }

    SECTION("Discovers eddy current as cartographer when cartographer object also present") {
        std::vector<std::string> objects = {"probe_eddy_current carto", "cartographer"};
        mgr().discover(objects);
        REQUIRE(mgr().sensor_count() >= 1);
        auto configs = mgr().get_sensors();
        bool found_carto = false;
        for (const auto& c : configs) {
            if (c.type == ProbeSensorType::CARTOGRAPHER)
                found_carto = true;
        }
        REQUIRE(found_carto);
    }

    SECTION("Discovers eddy current as beacon when beacon object also present") {
        std::vector<std::string> objects = {"probe_eddy_current beacon_probe", "beacon"};
        mgr().discover(objects);
        auto configs = mgr().get_sensors();
        bool found_beacon = false;
        for (const auto& c : configs) {
            if (c.type == ProbeSensorType::BEACON)
                found_beacon = true;
        }
        REQUIRE(found_beacon);
    }

    SECTION("Plain eddy current without companion stays EDDY_CURRENT") {
        std::vector<std::string> objects = {"probe_eddy_current btt"};
        mgr().discover(objects);
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].type == ProbeSensorType::EDDY_CURRENT);
    }

    SECTION("Cartographer with eddy current deduplicates to single sensor") {
        std::vector<std::string> objects = {"probe_eddy_current carto", "cartographer", "probe"};
        mgr().discover(objects);
        REQUIRE(mgr().sensor_count() == 1);
        REQUIRE(mgr().get_sensors()[0].klipper_name == "cartographer");
    }

    SECTION("Beacon with eddy current deduplicates to single sensor") {
        std::vector<std::string> objects = {"beacon", "probe_eddy_current beacon_probe", "probe"};
        mgr().discover(objects);
        REQUIRE(mgr().sensor_count() == 1);
        REQUIRE(mgr().get_sensors()[0].klipper_name == "beacon");
    }
}

// ============================================================================
// Macro-based Probe Detection Tests
// ============================================================================

TEST_CASE_METHOD(ProbeSensorTestFixture, "ProbeSensorManager - macro-based probe detection",
                 "[probe][discovery][macros]") {
    SECTION("Detects Klicky from ATTACH_PROBE/DOCK_PROBE macros") {
        std::vector<std::string> objects = {"probe", "gcode_macro ATTACH_PROBE",
                                            "gcode_macro DOCK_PROBE"};
        mgr().discover(objects);
        auto configs = mgr().get_sensors();
        REQUIRE(configs.size() == 1);
        REQUIRE(configs[0].type == ProbeSensorType::KLICKY);
    }

    SECTION("Detects Klicky from alternate macro names") {
        std::vector<std::string> objects = {"probe", "gcode_macro _Probe_Deploy",
                                            "gcode_macro _Probe_Stow"};
        mgr().discover(objects);
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].type == ProbeSensorType::KLICKY);
    }

    SECTION("Standard probe without Klicky macros stays STANDARD") {
        std::vector<std::string> objects = {"probe"};
        mgr().discover(objects);
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].type == ProbeSensorType::STANDARD);
    }

    SECTION("Standard probe with unrelated macros stays STANDARD") {
        std::vector<std::string> objects = {"probe", "gcode_macro START_PRINT",
                                            "gcode_macro END_PRINT"};
        mgr().discover(objects);
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].type == ProbeSensorType::STANDARD);
    }
}

// ============================================================================
// Probe Type Display String Tests
// ============================================================================

TEST_CASE("Probe type display strings", "[probe][types]") {
    REQUIRE(probe_type_to_display_string(ProbeSensorType::STANDARD) == "Probe");
    REQUIRE(probe_type_to_display_string(ProbeSensorType::BLTOUCH) == "BLTouch");
    REQUIRE(probe_type_to_display_string(ProbeSensorType::SMART_EFFECTOR) == "Smart Effector");
    REQUIRE(probe_type_to_display_string(ProbeSensorType::EDDY_CURRENT) == "Eddy Current");
    REQUIRE(probe_type_to_display_string(ProbeSensorType::CARTOGRAPHER) == "Cartographer");
    REQUIRE(probe_type_to_display_string(ProbeSensorType::BEACON) == "Beacon");
    REQUIRE(probe_type_to_display_string(ProbeSensorType::TAP) == "Voron Tap");
    REQUIRE(probe_type_to_display_string(ProbeSensorType::KLICKY) == "Klicky");
    REQUIRE(probe_type_to_display_string(ProbeSensorType::LOADCELL) == "Load Cell");
}

// ============================================================================
// Null Field Handling Tests
// ============================================================================

TEST_CASE_METHOD(ProbeSensorTestFixture, "ProbeSensorManager - null fields in status",
                 "[probe][state][loadcell]") {
    std::vector<std::string> objects = {"probe"};
    mgr().discover(objects);
    mgr().set_sensor_role("probe", ProbeSensorRole::Z_PROBE);

    SECTION("Null z_offset does not overwrite the config-seeded value") {
        json config;
        config["probe"] = {{"z_offset", "-0.250"}};
        mgr().discover_from_config(config);

        json null_status;
        null_status["probe"]["last_z_result"] = 0.1f;
        null_status["probe"]["z_offset"] = nullptr;
        mgr().update_from_status(null_status);

        auto state = mgr().get_sensor_state(ProbeSensorRole::Z_PROBE);
        REQUIRE(state->z_offset == Catch::Approx(-0.25f));
        REQUIRE(state->last_z_result == Catch::Approx(0.1f));
    }

    SECTION("Null last_z_result does not overwrite existing value") {
        json status;
        status["probe"]["last_z_result"] = 0.5f;
        mgr().update_from_status(status);

        json null_status;
        null_status["probe"]["last_z_result"] = nullptr;
        mgr().update_from_status(null_status);

        auto state = mgr().get_sensor_state(ProbeSensorRole::Z_PROBE);
        REQUIRE(state->last_z_result == Catch::Approx(0.5f));
    }
}

// ============================================================================
// Cartographer Status Source Tests
// ============================================================================

TEST_CASE_METHOD(ProbeSensorTestFixture,
                 "ProbeSensorManager - Cartographer reads the probe object it registers",
                 "[probe][state][cartographer]") {
    mgr().discover({"probe", "cartographer"});
    REQUIRE(mgr().sensor_count() == 1);
    mgr().set_sensor_role("cartographer", ProbeSensorRole::Z_PROBE);

    // The cartographer object nests last_z_result per mode; the flat keys live
    // on the probe object.
    json status;
    status["cartographer"] = {{"scan", {{"last_z_result", 9.0}}}, {"touch", nullptr}};
    status["probe"] = {{"name", "cartographer"}, {"last_query", 1}, {"last_z_result", -0.425}};
    mgr().update_from_status(status);

    auto state = mgr().get_sensor_state(ProbeSensorRole::Z_PROBE);
    REQUIRE(state->last_z_result == Catch::Approx(-0.425f));
    REQUIRE(state->triggered);
    REQUIRE(lv_subject_get_int(mgr().get_probe_triggered_subject()) == 1);
}

// ============================================================================
// discover_from_config Tests
// ============================================================================

TEST_CASE_METHOD(ProbeSensorTestFixture, "ProbeSensorManager - discover_from_config seeds z_offset",
                 "[probe][discovery][config]") {
    std::vector<std::string> objects = {"probe"};
    mgr().discover(objects);
    mgr().set_sensor_role("probe", ProbeSensorRole::Z_PROBE);

    SECTION("Seeds z_offset from config string value") {
        json config;
        config["probe"] = {{"z_offset", "-0.250"}};
        mgr().discover_from_config(config);

        auto state = mgr().get_sensor_state(ProbeSensorRole::Z_PROBE);
        REQUIRE(state->z_offset == Catch::Approx(-0.25f));
        REQUIRE(lv_subject_get_int(mgr().get_probe_z_offset_subject()) == -250);
    }

    SECTION("Seeds z_offset from config numeric value") {
        json config;
        config["probe"] = {{"z_offset", -1.5f}};
        mgr().discover_from_config(config);

        auto state = mgr().get_sensor_state(ProbeSensorRole::Z_PROBE);
        REQUIRE(state->z_offset == Catch::Approx(-1.5f));
    }

    SECTION("Ignores config for unknown sensor") {
        json config;
        config["unknown_sensor"] = {{"z_offset", "-0.5"}};
        mgr().discover_from_config(config);

        auto state = mgr().get_sensor_state(ProbeSensorRole::Z_PROBE);
        REQUIRE(state->z_offset == Catch::Approx(0.0f));
    }

    SECTION("Config-seeded z_offset survives null status update") {
        json config;
        config["probe"] = {{"z_offset", "-0.250"}};
        mgr().discover_from_config(config);

        // Null z_offset status should not overwrite config-seeded value
        json status;
        status["probe"]["last_z_result"] = 0.0f;
        status["probe"]["z_offset"] = nullptr;
        mgr().update_from_status(status);

        auto state = mgr().get_sensor_state(ProbeSensorRole::Z_PROBE);
        REQUIRE(state->z_offset == Catch::Approx(-0.25f));
    }
}

// A deferral queued before deinit_subjects() must not land on the subjects a
// later init_subjects() creates.
TEST_CASE_METHOD(ProbeSensorTestFixture,
                 "ProbeSensorManager - deferred update dropped after deinit", "[probe][lifetime]") {
    discover_test_sensors();
    mgr().set_sensor_role("bltouch", ProbeSensorRole::Z_PROBE);
    mgr().set_sync_mode(false);

    update_sensor_state("bltouch", 0.25f);
    mgr().deinit_subjects();
    mgr().init_subjects();
    helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
    REQUIRE(lv_subject_get_int(mgr().get_probe_last_z_subject()) == -1);

    update_sensor_state("bltouch", 0.5f);
    helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
    REQUIRE(lv_subject_get_int(mgr().get_probe_last_z_subject()) == 500);
    mgr().set_sync_mode(true);
}

// Klipper's bltouch/smart_effector/probe_eddy_current, Beacon and Cartographer all
// register the generic probe object beside their own. The pair is one probe, so a fresh
// install gets Z_PROBE without anyone picking it.
TEST_CASE_METHOD(ProbeSensorTestFixture,
                 "ProbeSensorManager - alias objects still auto-assign Z_PROBE", "[probe][roles]") {
    Config::get_instance()->get_json(Config::get_instance()->df() + "probe_sensors") =
        nlohmann::json::object();

    SECTION("bltouch plus its probe alias") {
        mgr().discover({"bltouch", "probe"});
        mgr().load_config_from_file();
        REQUIRE(mgr().get_sensors()[0].klipper_name == "bltouch");
        update_sensor_state("bltouch", 0.1f);
        REQUIRE(lv_subject_get_int(mgr().get_probe_last_z_subject()) == 100);
    }

    SECTION("cartographer plus an eddy object and its probe object") {
        mgr().discover({"probe", "probe_eddy_current carto", "cartographer"});
        mgr().load_config_from_file();
        update_sensor_state("probe", -0.4f);
        REQUIRE(lv_subject_get_int(mgr().get_probe_last_z_subject()) == -400);
    }

    SECTION("plain probe alone") {
        mgr().discover({"probe"});
        mgr().load_config_from_file();
        update_sensor_state("probe", 0.25f);
        REQUIRE(lv_subject_get_int(mgr().get_probe_last_z_subject()) == 250);
    }

    SECTION("a saved explicit role still wins") {
        Config::get_instance()->get_json(Config::get_instance()->df() + "probe_sensors") =
            nlohmann::json{{"sensors", nlohmann::json::array({{{"klipper_name", "smart_effector"},
                                                               {"role", "z_probe"}}})}};
        mgr().discover({"bltouch", "smart_effector", "probe"});
        mgr().load_config_from_file();
        auto configs = mgr().get_sensors();
        REQUIRE(configs.size() == 2);
        for (const auto& c : configs) {
            CAPTURE(c.klipper_name);
            REQUIRE((c.role == ProbeSensorRole::Z_PROBE) == (c.klipper_name == "smart_effector"));
        }
    }

    Config::get_instance()->get_json(Config::get_instance()->df() + "probe_sensors") =
        nlohmann::json::object();
}
