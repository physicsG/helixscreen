// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_accel_sensor_manager.cpp
 * @brief Unit tests for AccelSensorManager
 *
 * Tests cover:
 * - Type helpers: role/type string conversion
 * - Sensor discovery from Klipper object names (adxl345, lis2dw, lis3dh, mpu9250, icm20948)
 * - Role assignment (INPUT_SHAPER)
 * - Status frames leave discovered state alone (accelerometers publish none)
 * - Config persistence
 */

#include "../ui_test_utils.h"
#include "accel_sensor_manager.h"
#include "accel_sensor_types.h"

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
class AccelSensorManagerTestAccess {
  public:
    static void reset(AccelSensorManager& obj) {
        std::lock_guard<std::recursive_mutex> lock(obj.mutex_);
        obj.sensors_.clear();
        obj.deinit_subjects();
    }
};
} // namespace helix::sensors

// ============================================================================
// Test Fixture
// ============================================================================

class AccelSensorTestFixture {
  public:
    AccelSensorTestFixture() {
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

        // Reset state for test isolation first
        AccelSensorManagerTestAccess::reset(mgr());

        // Initialize subjects after reset (reset_for_testing deinits subjects)
        mgr().init_subjects();
    }

    ~AccelSensorTestFixture() {
        // Reset after each test
        AccelSensorManagerTestAccess::reset(mgr());
    }

  protected:
    AccelSensorManager& mgr() {
        return AccelSensorManager::instance();
    }

    // Helper to discover standard test sensors using config keys (how production works)
    void discover_test_sensors() {
        json config = {{"adxl345", json::object()},
                       {"adxl345 bed", json::object()},
                       {"lis2dw hotend", json::object()}};
        mgr().discover_from_config(config);
    }

  private:
    static lv_display_t* display_;
    static bool display_created_;
};

// Static members
lv_display_t* AccelSensorTestFixture::display_ = nullptr;
bool AccelSensorTestFixture::display_created_ = false;

// ============================================================================
// Type Helper Tests (accel_sensor_types.h)
// ============================================================================

TEST_CASE("AccelSensorTypes - role string conversion", "[accel][types]") {
    SECTION("accel_role_to_string") {
        REQUIRE(accel_role_to_string(AccelSensorRole::NONE) == "none");
        REQUIRE(accel_role_to_string(AccelSensorRole::INPUT_SHAPER) == "input_shaper");
    }

    SECTION("accel_role_from_string") {
        REQUIRE(accel_role_from_string("none") == AccelSensorRole::NONE);
        REQUIRE(accel_role_from_string("input_shaper") == AccelSensorRole::INPUT_SHAPER);
        REQUIRE(accel_role_from_string("invalid") == AccelSensorRole::NONE);
        REQUIRE(accel_role_from_string("") == AccelSensorRole::NONE);
    }

    SECTION("accel_role_to_display_string") {
        REQUIRE(accel_role_to_display_string(AccelSensorRole::NONE) == "Unassigned");
        REQUIRE(accel_role_to_display_string(AccelSensorRole::INPUT_SHAPER) == "Input Shaper");
    }
}

TEST_CASE("AccelSensorTypes - type string conversion", "[accel][types]") {
    SECTION("accel_type_to_string") {
        REQUIRE(accel_type_to_string(AccelSensorType::ADXL345) == "adxl345");
        REQUIRE(accel_type_to_string(AccelSensorType::LIS2DW) == "lis2dw");
        REQUIRE(accel_type_to_string(AccelSensorType::LIS3DH) == "lis3dh");
        REQUIRE(accel_type_to_string(AccelSensorType::MPU9250) == "mpu9250");
        REQUIRE(accel_type_to_string(AccelSensorType::ICM20948) == "icm20948");
    }

    SECTION("accel_type_from_string") {
        REQUIRE(accel_type_from_string("adxl345") == AccelSensorType::ADXL345);
        REQUIRE(accel_type_from_string("lis2dw") == AccelSensorType::LIS2DW);
        REQUIRE(accel_type_from_string("lis3dh") == AccelSensorType::LIS3DH);
        REQUIRE(accel_type_from_string("mpu9250") == AccelSensorType::MPU9250);
        REQUIRE(accel_type_from_string("icm20948") == AccelSensorType::ICM20948);
        REQUIRE(accel_type_from_string("invalid") == AccelSensorType::ADXL345);
        REQUIRE(accel_type_from_string("") == AccelSensorType::ADXL345);
    }
}

// ============================================================================
// Config-based Discovery Tests (NEW - accelerometers come from configfile.config)
// ============================================================================

TEST_CASE_METHOD(AccelSensorTestFixture, "AccelSensorManager - config-based discovery",
                 "[accel][discovery][config]") {
    using json = nlohmann::json;

    SECTION("Discovers ADXL345 from config keys") {
        json config_keys = {{"adxl345", json::object()}};
        mgr().discover_from_config(config_keys);

        REQUIRE(mgr().has_sensors());
        REQUIRE(mgr().sensor_count() == 1);

        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].klipper_name == "adxl345");
        REQUIRE(configs[0].sensor_name == "adxl345");
        REQUIRE(configs[0].type == AccelSensorType::ADXL345);
    }

    SECTION("Discovers named ADXL345 from config keys") {
        json config_keys = {{"adxl345 bed", json::object()}};
        mgr().discover_from_config(config_keys);

        REQUIRE(mgr().sensor_count() == 1);

        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].klipper_name == "adxl345 bed");
        REQUIRE(configs[0].sensor_name == "bed");
        REQUIRE(configs[0].type == AccelSensorType::ADXL345);
    }

    SECTION("Discovers multiple accelerometers from config") {
        json config_keys = {{"adxl345", json::object()},
                            {"adxl345 bed", json::object()},
                            {"lis2dw hotend", json::object()},
                            {"resonance_tester", json::object()}, // Should be ignored
                            {"stepper_x", json::object()}};       // Should be ignored
        mgr().discover_from_config(config_keys);

        REQUIRE(mgr().sensor_count() == 3);

        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].klipper_name == "adxl345");
        REQUIRE(configs[1].klipper_name == "adxl345 bed");
        REQUIRE(configs[2].klipper_name == "lis2dw hotend");
    }

    SECTION("Handles empty config keys") {
        json config_keys = json::object();
        mgr().discover_from_config(config_keys);
        REQUIRE_FALSE(mgr().has_sensors());
    }

    SECTION("Ignores non-accelerometer config keys") {
        json config_keys = {{"stepper_x", json::object()},
                            {"extruder", json::object()},
                            {"resonance_tester", json::object()}};
        mgr().discover_from_config(config_keys);
        REQUIRE_FALSE(mgr().has_sensors());
    }

    SECTION("Discovers LIS3DH from config keys") {
        json config_keys = {{"lis3dh", json::object()}};
        mgr().discover_from_config(config_keys);

        REQUIRE(mgr().sensor_count() == 1);
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].klipper_name == "lis3dh");
        REQUIRE(configs[0].type == AccelSensorType::LIS3DH);
    }

    SECTION("Discovers named LIS3DH from config keys") {
        json config_keys = {{"lis3dh toolhead", json::object()}};
        mgr().discover_from_config(config_keys);

        REQUIRE(mgr().sensor_count() == 1);
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].klipper_name == "lis3dh toolhead");
        REQUIRE(configs[0].sensor_name == "toolhead");
        REQUIRE(configs[0].type == AccelSensorType::LIS3DH);
    }

    SECTION("Discovers ICM20948 from config keys") {
        json config_keys = {{"icm20948", json::object()}};
        mgr().discover_from_config(config_keys);

        REQUIRE(mgr().sensor_count() == 1);
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].klipper_name == "icm20948");
        REQUIRE(configs[0].type == AccelSensorType::ICM20948);
    }

    SECTION("Discovers Beacon onboard accelerometer via accel_scale") {
        json config_keys = {{"beacon", {{"accel_scale", "16g"}, {"serial", "/dev/ttyUSB0"}}}};
        mgr().discover_from_config(config_keys);

        REQUIRE(mgr().sensor_count() == 1);
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].klipper_name == "beacon");
        REQUIRE(configs[0].sensor_name == "beacon");
        REQUIRE(configs[0].type == AccelSensorType::LIS2DW);
    }

    SECTION("Discovers Beacon onboard accelerometer via accel_axes_map") {
        json config_keys = {{"beacon", {{"accel_axes_map", "x,y,z"}}}};
        mgr().discover_from_config(config_keys);

        REQUIRE(mgr().sensor_count() == 1);
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].klipper_name == "beacon");
        REQUIRE(configs[0].type == AccelSensorType::LIS2DW);
    }

    SECTION("Beacon without accel fields is not discovered as accelerometer") {
        json config_keys = {{"beacon", {{"serial", "/dev/serial/by-id/usb-beacon"}}}};
        mgr().discover_from_config(config_keys);

        REQUIRE_FALSE(mgr().has_sensors());
    }

    SECTION("Discovers Beacon accelerometer via resonance_tester accel_chip") {
        json config_keys = {
            {"resonance_tester", {{"accel_chip", "beacon"}, {"probe_points", "100,100,20"}}}};
        mgr().discover_from_config(config_keys);

        REQUIRE(mgr().sensor_count() == 1);
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].klipper_name == "beacon");
        REQUIRE(configs[0].type == AccelSensorType::LIS2DW);
    }

    SECTION("Beacon found via resonance_tester survives rediscovery") {
        json config_keys = {{"resonance_tester", {{"accel_chip", "beacon"}}}};
        mgr().discover_from_config(config_keys);
        mgr().discover_from_config(config_keys);

        REQUIRE(mgr().sensor_count() == 1);
        REQUIRE(mgr().get_sensors()[0].klipper_name == "beacon");
    }

    SECTION("Discovers Beacon accelerometer via resonance_tester accel_chip_x") {
        json config_keys = {{"resonance_tester", {{"accel_chip_x", "beacon"}}}};
        mgr().discover_from_config(config_keys);

        REQUIRE(mgr().sensor_count() == 1);
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].klipper_name == "beacon");
    }

    SECTION("Beacon accel_scale takes priority over resonance_tester fallback") {
        // Both beacon config and resonance_tester reference beacon —
        // should only register once
        json config_keys = {{"beacon", {{"accel_scale", "16g"}}},
                            {"resonance_tester", {{"accel_chip", "beacon"}}}};
        mgr().discover_from_config(config_keys);

        // Should have exactly 1 beacon sensor, not 2
        int beacon_count = 0;
        for (const auto& s : mgr().get_sensors()) {
            if (s.klipper_name == "beacon") {
                beacon_count++;
            }
        }
        REQUIRE(beacon_count == 1);
    }
}

// Accelerometers only exist in configfile.config, not printer.objects.list, so
// discover_from_config() is the only discovery path.

// ============================================================================
// Role Assignment Tests
// ============================================================================

TEST_CASE_METHOD(AccelSensorTestFixture, "AccelSensorManager - role assignment", "[accel][roles]") {
    discover_test_sensors();

    SECTION("Can set INPUT_SHAPER role") {
        mgr().set_sensor_role("adxl345", AccelSensorRole::INPUT_SHAPER);

        auto configs = mgr().get_sensors();
        auto it = std::find_if(configs.begin(), configs.end(),
                               [](const auto& c) { return c.klipper_name == "adxl345"; });
        REQUIRE(it != configs.end());
        REQUIRE(it->role == AccelSensorRole::INPUT_SHAPER);
    }

    SECTION("Role assignment is unique - assigning same role clears previous") {
        mgr().set_sensor_role("adxl345", AccelSensorRole::INPUT_SHAPER);
        mgr().set_sensor_role("adxl345 bed", AccelSensorRole::INPUT_SHAPER);

        auto configs = mgr().get_sensors();

        auto adxl_it = std::find_if(configs.begin(), configs.end(),
                                    [](const auto& c) { return c.klipper_name == "adxl345"; });
        REQUIRE(adxl_it->role == AccelSensorRole::NONE);

        auto bed_it = std::find_if(configs.begin(), configs.end(),
                                   [](const auto& c) { return c.klipper_name == "adxl345 bed"; });
        REQUIRE(bed_it->role == AccelSensorRole::INPUT_SHAPER);
    }

    SECTION("Can assign NONE without affecting other sensors") {
        mgr().set_sensor_role("adxl345", AccelSensorRole::INPUT_SHAPER);

        mgr().set_sensor_role("adxl345", AccelSensorRole::NONE);

        auto configs = mgr().get_sensors();
        auto it = std::find_if(configs.begin(), configs.end(),
                               [](const auto& c) { return c.klipper_name == "adxl345"; });
        REQUIRE(it->role == AccelSensorRole::NONE);
    }

    SECTION("Assigning role to unknown sensor does nothing") {
        mgr().set_sensor_role("nonexistent_sensor", AccelSensorRole::INPUT_SHAPER);

        for (const auto& config : mgr().get_sensors()) {
            REQUIRE(config.role == AccelSensorRole::NONE);
        }
    }
}

// ============================================================================
// State Update Tests
// ============================================================================

TEST_CASE_METHOD(AccelSensorTestFixture, "AccelSensorManager - status frames change nothing",
                 "[accel][state]") {
    discover_test_sensors();
    mgr().set_sensor_role("adxl345", AccelSensorRole::INPUT_SHAPER);
    REQUIRE(mgr().is_sensor_available(AccelSensorRole::INPUT_SHAPER));

    json status;
    status["adxl345"]["connected"] = false;
    status["unknown_sensor"]["connected"] = true;
    mgr().update_from_status(status);
    mgr().update_from_status(json::object());

    REQUIRE(mgr().sensor_count() == 3);
    REQUIRE(mgr().is_sensor_available(AccelSensorRole::INPUT_SHAPER));
    REQUIRE(lv_subject_get_int(mgr().get_sensor_count_subject()) == 3);
}

// ============================================================================
// Config Persistence Tests
// ============================================================================

TEST_CASE_METHOD(AccelSensorTestFixture, "AccelSensorManager - config persistence",
                 "[accel][config]") {
    discover_test_sensors();

    SECTION("save_config returns JSON with role assignments") {
        mgr().set_sensor_role("adxl345", AccelSensorRole::INPUT_SHAPER);

        json config = mgr().save_config();

        REQUIRE(config.is_object());
        REQUIRE(config.contains("sensors"));
        REQUIRE(config["sensors"].is_array());
        REQUIRE(config["sensors"].size() == 3);

        bool found_adxl = false;
        for (const auto& sensor : config["sensors"]) {
            if (sensor["klipper_name"] == "adxl345") {
                REQUIRE(sensor["role"] == "input_shaper");
                found_adxl = true;
            }
        }
        REQUIRE(found_adxl);
    }

    SECTION("load_config restores role assignments") {
        // Set up config JSON
        json config;
        json sensors_array = json::array();
        json sensor1;
        sensor1["klipper_name"] = "adxl345";
        sensor1["role"] = "input_shaper";
        sensor1["enabled"] = true;
        sensors_array.push_back(sensor1);
        config["sensors"] = sensors_array;

        mgr().load_config(config);

        auto configs = mgr().get_sensors();
        auto it = std::find_if(configs.begin(), configs.end(),
                               [](const auto& c) { return c.klipper_name == "adxl345"; });
        REQUIRE(it != configs.end());
        REQUIRE(it->role == AccelSensorRole::INPUT_SHAPER);
    }

    SECTION("load_config with unknown sensor is handled gracefully") {
        json config;
        json sensors_array = json::array();
        json sensor1;
        sensor1["klipper_name"] = "unknown_sensor";
        sensor1["role"] = "input_shaper";
        sensors_array.push_back(sensor1);
        config["sensors"] = sensors_array;

        // Should not crash
        mgr().load_config(config);

        // Existing sensors should be unaffected
        for (const auto& sensor : mgr().get_sensors()) {
            REQUIRE(sensor.role == AccelSensorRole::NONE);
        }
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE_METHOD(AccelSensorTestFixture, "AccelSensorManager - edge cases", "[accel][edge]") {
    SECTION("get_sensor_state returns nullopt for unassigned role") {
        discover_test_sensors();
        auto state = mgr().get_sensor_state(AccelSensorRole::INPUT_SHAPER);
        REQUIRE_FALSE(state.has_value());
    }

    SECTION("get_sensor_state returns nullopt for NONE role") {
        discover_test_sensors();
        auto state = mgr().get_sensor_state(AccelSensorRole::NONE);
        REQUIRE_FALSE(state.has_value());
    }

    SECTION("is_sensor_available checks role assignment and enabled") {
        discover_test_sensors();
        REQUIRE_FALSE(mgr().is_sensor_available(AccelSensorRole::INPUT_SHAPER));

        mgr().set_sensor_role("adxl345", AccelSensorRole::INPUT_SHAPER);
        REQUIRE(mgr().is_sensor_available(AccelSensorRole::INPUT_SHAPER));

        mgr().set_sensor_enabled("adxl345", false);
        REQUIRE_FALSE(mgr().is_sensor_available(AccelSensorRole::INPUT_SHAPER));
    }
}
