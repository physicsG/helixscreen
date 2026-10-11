// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_settings_sensors.h
 * @brief Sensor Settings overlay - configure all sensor types
 *
 * This overlay allows users to configure sensors across multiple categories:
 * - Switch sensors (filament runout/motion) with master enable toggle
 * - Probe sensors (Z probes, BLTouch, etc.)
 * - Width sensors (filament diameter)
 * - Humidity sensors (chamber/dryer)
 * - Accelerometer sensors (input shaper)
 *
 * Each sensor type has its own section, hidden when no sensors of that type exist.
 *
 * @pattern Overlay (lazy init, dynamic row creation)
 * @threading Main thread only
 *
 * @see FilamentSensorManager, ProbeSensorManager, WidthSensorManager, etc.
 * @see Config for persistence
 */

#pragma once

#include "lvgl/lvgl.h"
#include "overlay_base.h"
#include "static_panel_registry.h"

#include <string>
#include <vector>

// Forward declarations - avoid including all sensor headers
namespace helix {
struct FilamentSensorConfig;
}

namespace helix::sensors {
struct ProbeSensorConfig;
struct WidthSensorConfig;
struct HumiditySensorConfig;
struct AccelSensorConfig;
struct TemperatureSensorConfig;
} // namespace helix::sensors

namespace helix::settings {

/**
 * @class SensorSettingsOverlay
 * @brief Overlay for configuring all sensor types
 *
 * This overlay provides sections for each sensor category:
 * - Switch sensors: Master toggle + per-sensor role dropdown + enable toggle
 * - Probe sensors: Display-only (role assigned via wizard)
 * - Width sensors: Role dropdown for flow compensation
 * - Humidity sensors: Role dropdown for chamber/dryer
 * - Accelerometers: Display-only status
 *
 * ## State Management:
 *
 * Configuration is managed by respective sensor manager singletons.
 * Changes are immediately persisted via mgr.save_config().
 *
 * ## Usage:
 *
 * @code
 * auto& overlay = helix::settings::get_sensor_settings_overlay();
 * overlay.show(parent_screen);  // Creates overlay if needed, populates lists, shows
 * @endcode
 *
 * ## Note on Dynamic Row Creation:
 *
 * Unlike declarative XML callbacks, sensor rows are created dynamically at runtime
 * using lv_xml_create(). This requires using lv_obj_add_event_cb() for the
 * dropdown and toggle callbacks, which is an acceptable exception to the
 * declarative UI rule.
 */
class SensorSettingsOverlay : public OverlayBase {
  public:
    /**
     * @brief Register event callbacks with lv_xml system
     *
     * Registers callbacks for:
     * - on_switch_master_toggle_changed (switch sensor master enable)
     * - on_chamber_heater_changed / on_chamber_sensor_changed (chamber assignment)
     */
    void register_callbacks() override;

    const char* get_name() const override {
        return "Sensors";
    }
    const char* xml_component() const override {
        return "sensors_overlay";
    }

    /**
     * @brief Called when overlay becomes visible
     *
     * Calls OverlayBase::on_activate() then populates all sensor lists.
     */
    void on_activate() override;

    //
    // === Event Handlers (public for static callbacks) ===
    //

    /**
     * @brief Handle switch sensor master enable toggle change
     * @param enabled New master enable state
     */
    void handle_switch_master_toggle_changed(bool enabled);

  protected:
    /// Refreshes the section count badges on every open.
    void before_show() override;

  private:
    //
    // === Internal Methods - Switch Sensors ===
    //

    /**
     * @brief Populate switch sensor list from FilamentSensorManager
     *
     * Creates a filament_sensor_row component for each discovered sensor.
     * Clears existing rows first to allow refresh.
     */
    void populate_switch_sensors();

    /**
     * @brief Update switch sensor count label
     */
    void update_switch_sensor_count();

    /**
     * @brief Get switch sensors excluding AMS/multi-material types
     * @return Vector of standalone sensors only
     */
    [[nodiscard]] std::vector<helix::FilamentSensorConfig> get_standalone_switch_sensors() const;

    //
    // === Internal Methods - Other Sensor Types ===
    //

    /**
     * @brief Populate probe sensor list from ProbeSensorManager
     */
    void populate_probe_sensors();

    /**
     * @brief Update probe sensor count label
     */
    void update_probe_sensor_count();

    /**
     * @brief Populate width sensor list from WidthSensorManager
     */
    void populate_width_sensors();

    /**
     * @brief Update width sensor count label
     */
    void update_width_sensor_count();

    /**
     * @brief Populate humidity sensor list from HumiditySensorManager
     */
    void populate_humidity_sensors();

    /**
     * @brief Update humidity sensor count label
     */
    void update_humidity_sensor_count();

    /**
     * @brief Populate accelerometer list from AccelSensorManager
     */
    void populate_accel_sensors();

    /**
     * @brief Update accelerometer count label
     */
    void update_accel_sensor_count();

    /**
     * @brief Populate chamber assignment dropdowns from PrinterDiscovery
     */
    void populate_chamber_assignment();

    /**
     * @brief Populate load cell list from LoadCellManager
     */
    void populate_load_cells();

    /**
     * @brief Update load cell count label
     */
    void update_load_cell_count();

    /**
     * @brief Populate temperature sensor list from TemperatureSensorManager
     */
    void populate_temperature_sensors();

    /**
     * @brief Update temperature sensor count label
     */
    void update_temperature_sensor_count();

    /**
     * @brief Populate all sensor lists
     */
    void populate_all_sensors();

    /**
     * @brief Update all sensor count labels
     */
    void update_all_sensor_counts();

    //
    // === State ===
    //
    // Note: overlay_root_ and parent_screen_ are inherited from OverlayBase

    /// Klipper object behind each chamber dropdown's option index i + 1, rebuilt
    /// with the options on every populate.
    std::vector<std::string> chamber_heater_names_;
    std::vector<std::string> chamber_sensor_names_;
};

inline SensorSettingsOverlay& get_sensor_settings_overlay() {
    return lazy_global<SensorSettingsOverlay>("SensorSettingsOverlay");
}

struct ChamberAssignmentLabels;

/// Translated labels both chamber assignment dropdowns are built from.
ChamberAssignmentLabels chamber_assignment_labels();

} // namespace helix::settings
