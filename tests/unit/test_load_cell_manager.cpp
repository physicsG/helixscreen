// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_load_cell_manager.cpp
 * @brief Unit tests for LoadCellManager
 *
 * Tests cover:
 * - Sensor discovery from Klipper object names (load_cell)
 * - Role assignment (SPOOL_WEIGHT)
 * - State updates from Moonraker status JSON
 */

#include "../ui_test_utils.h"
#include "ams_state.h"
#include "config.h"
#include "lane_source_store.h"
#include "load_cell_manager.h"

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
class LoadCellManagerTestAccess {
  public:
    static void reset(LoadCellManager& obj) {
        std::lock_guard<std::recursive_mutex> lock(obj.mutex_);
        obj.sensors_.clear();
        obj.sync_mode_ = true;
        obj.deinit_subjects();
    }
};
} // namespace helix::sensors

// ============================================================================
// Test Fixture
// ============================================================================

class LoadCellTestFixture {
  public:
    LoadCellTestFixture() {
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
                                        (void)area;
                                        (void)px_map;
                                    });
            display_created_ = true;
        }

        // Reset state for test isolation first
        LoadCellManagerTestAccess::reset(mgr());

        // Initialize subjects after reset (reset_for_testing deinits subjects)
        mgr().init_subjects();

        Config::get_instance()->reset_to_defaults();
        AmsState::instance().clear_external_spool_info();
    }

    ~LoadCellTestFixture() {
        // Reset after each test
        LoadCellManagerTestAccess::reset(mgr());
        AmsState::instance().clear_external_spool_info();
    }

  protected:
    LoadCellManager& mgr() {
        return LoadCellManager::instance();
    }

    // Helper to discover standard test sensors using config keys (how production works)
    void discover_test_sensors() {
        std::vector<std::string> klipper_objects = {"load_cell", "load_cell spool_weight"};
        mgr().discover(klipper_objects);
    }

    // Helper to simulate Moonraker status update
    void update_sensor_state(const std::string& klipper_name, float force_g) {
        json status;
        status[klipper_name]["force_g"] = force_g;
        mgr().update_from_status(status);
    }

    // The resolved record, which is what every view reads.
    SlotInfo ams_state_spool_info() {
        const auto info = AmsState::instance().get_external_spool_info();
        REQUIRE(info.has_value());
        return *info;
    }

    bool has_external_spool() {
        return AmsState::instance().get_external_spool_info().has_value();
    }

  private:
    static lv_display_t* display_;
    static bool display_created_;
};

// Static members
lv_display_t* LoadCellTestFixture::display_ = nullptr;
bool LoadCellTestFixture::display_created_ = false;

// ============================================================================
// State Update Tests
// ============================================================================

TEST_CASE_METHOD(LoadCellTestFixture, "LoadCellManager - state updates", "[load_cell][state]") {
    discover_test_sensors();

    SECTION("Discovers unnamed and named sensors") {
        REQUIRE(mgr().sensor_count() == 2);
    }

    SECTION("Treats a single unnamed load cell as spool weight") {
        std::vector<std::string> klipper_objects = {"load_cell"};
        mgr().discover(klipper_objects);
        REQUIRE(mgr().sensor_count() == 1);

        update_sensor_state("load_cell", 300.0);
        REQUIRE(ams_state_spool_info().remaining_weight_g == 300.0);
    }

    SECTION("Treats a single named load cell as spool weight, regardless of its name") {
        std::vector<std::string> klipper_objects = {"load_cell name_does_not_matter"};
        mgr().discover(klipper_objects);
        REQUIRE(mgr().sensor_count() == 1);

        update_sensor_state("load_cell name_does_not_matter", 300.0);
        REQUIRE(ams_state_spool_info().remaining_weight_g == 300.0);
    }

    SECTION("Publishes spool weight update to AmsState") {
        update_sensor_state("load_cell spool_weight", 300.0);
        REQUIRE(ams_state_spool_info().remaining_weight_g == 300.0);
    }

    SECTION("Does not publish spool weight update for load cells "
            "that don't have the SPOOL_WEIGHT role") {
        update_sensor_state("load_cell", 300.0);
        REQUIRE(!has_external_spool());
    }

    SECTION("Publishes update only if spool weight decreased by at least 0.5 g") {
        REQUIRE(!has_external_spool());

        update_sensor_state("load_cell spool_weight", 300.0);
        REQUIRE(ams_state_spool_info().remaining_weight_g == 300.0);

        update_sensor_state("load_cell spool_weight", 299.6);
        REQUIRE(ams_state_spool_info().remaining_weight_g == 300.0);

        update_sensor_state("load_cell spool_weight", 299.5);
        REQUIRE(ams_state_spool_info().remaining_weight_g == 299.5);
    }

    SECTION("Publishes update only if spool weight increased by at least 1 g") {
        update_sensor_state("load_cell spool_weight", 300.0);
        REQUIRE(ams_state_spool_info().remaining_weight_g == 300.0);

        update_sensor_state("load_cell spool_weight", 300.9);
        REQUIRE(ams_state_spool_info().remaining_weight_g == 300.0);

        update_sensor_state("load_cell spool_weight", 301.0);
        REQUIRE(ams_state_spool_info().remaining_weight_g == 301.0);
    }

    SECTION("Shows the reading over an earlier user edit of the weight") {
        SlotInfo spool;
        spool.material = "PLA";
        AmsState::instance().set_external_spool_info_in_memory(spool);
        helix::ams::Observation edit(helix::ams::ObservationSource::LocalUser);
        edit.remaining_weight_g = 750.0f;
        helix::ams::commit_slot_edit(helix::ams::BYPASS_LANE_ID, edit);
        REQUIRE(ams_state_spool_info().remaining_weight_g == 750.0);

        update_sensor_state("load_cell spool_weight", 290.0);
        REQUIRE(ams_state_spool_info().remaining_weight_g == 290.0);
    }

    SECTION("A frame without a usable force_g keeps the published weight") {
        update_sensor_state("load_cell spool_weight", 300.0);

        json missing;
        missing["load_cell spool_weight"] = json::object();
        mgr().update_from_status(missing);
        REQUIRE(ams_state_spool_info().remaining_weight_g == 300.0);

        json null_value;
        null_value["load_cell spool_weight"]["force_g"] = nullptr;
        mgr().update_from_status(null_value);
        REQUIRE(ams_state_spool_info().remaining_weight_g == 300.0);
    }

    SECTION("Empty status update is handled") {
        json status = json::object();
        mgr().update_from_status(status);

        REQUIRE(mgr().sensor_count() == 2);
    }
}
