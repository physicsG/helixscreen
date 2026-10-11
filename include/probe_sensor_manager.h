// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "async_lifetime_guard.h"
#include "lvgl.h"
#include "probe_sensor_types.h"
#include "sensor_collection.h"
#include "subject_managed_panel.h"

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "hv/json.hpp"

namespace helix::sensors {

/**
 * @brief Manager for native Klipper probe sensors
 *
 * Provides:
 * - Auto-discovery of probe sensors from Klipper objects list
 * - Role assignment for Z probing
 * - Real-time state tracking from Moonraker updates
 * - LVGL subjects for reactive UI binding
 *
 * Thread-safe for state updates from Moonraker callbacks.
 *
 * Klipper object names:
 * - probe - Standard probe
 * - bltouch - BLTouch probe
 * - smart_effector - Duet Smart Effector
 * - probe_eddy_current <name> - Eddy current probe (has a name parameter)
 * - beacon, cartographer - eddy scanners from their own plugins
 *
 * Status keys read: last_z_result, last_query (the last QUERY_PROBE result) and
 * z_offset. Which object carries them differs per type. Mainline modules do not
 * publish z_offset, so it is seeded from the configfile; the Creality K1/K2 and
 * QIDI forks publish it on their probe. Per-type table:
 * docs/devel/SENSOR_MANAGEMENT.md.
 *
 * @note Switch sensors configured as probes are handled by SwitchSensorManager,
 *       not this manager.
 */
class ProbeSensorManager {
  public:
    /**
     * @brief Get singleton instance
     */
    static ProbeSensorManager& instance();

    // Prevent copying
    ProbeSensorManager(const ProbeSensorManager&) = delete;
    ProbeSensorManager& operator=(const ProbeSensorManager&) = delete;

    // ========================================================================
    // Discovery, Status and Config
    // ========================================================================

    /**
     * @brief Discover sensors from Klipper objects list
     * @note MUST be called from main LVGL thread (updates subjects directly)
     */
    void discover(const std::vector<std::string>& klipper_objects);

    /// @brief Update state from Moonraker status JSON
    void update_from_status(const nlohmann::json& status);

    /**
     * @brief Status subscription for every probe object in an objects list
     *
     * Maps each probe probes_in() keeps to the object and fields its module
     * actually publishes: a Cartographer is read from the probe object it
     * registers, and types without a usable last_query skip it. z_offset is
     * requested everywhere; a module without it answers null.
     */
    [[nodiscard]] static nlohmann::json
    required_status_objects(const std::vector<std::string>& klipper_objects);

    /// @brief Seed initial state from Klipper configfile (e.g., z_offset from [probe])
    void discover_from_config(const nlohmann::json& config_keys);

    /**
     * @brief Load sensor configuration from JSON
     * @note MUST be called from main LVGL thread (updates subjects directly)
     */
    void load_config(const nlohmann::json& config);

    /// @brief Save configuration to JSON
    [[nodiscard]] nlohmann::json save_config() const;

    /// @brief Load sensor config (roles, enabled state) from settings.json
    void load_config_from_file();

    /// @brief Save sensor config (roles, enabled state) to settings.json
    void save_config_to_file();

    // ========================================================================
    // Initialization
    // ========================================================================

    /**
     * @brief Initialize LVGL subjects for UI binding
     *
     * Must be called before creating any XML components that bind to sensor subjects.
     * Safe to call multiple times (idempotent).
     */
    void init_subjects();

    /**
     * @brief Deinitialize LVGL subjects
     *
     * Must be called before lv_deinit() to properly disconnect observers.
     */
    void deinit_subjects();

    // ========================================================================
    // Sensor Queries
    // ========================================================================

    /**
     * @brief Check if any sensors have been discovered
     */
    [[nodiscard]] bool has_sensors() const;

    /**
     * @brief Get all discovered sensor configurations (thread-safe copy)
     */
    [[nodiscard]] std::vector<ProbeSensorConfig> get_sensors() const;

    /**
     * @brief Get sensor count
     */
    [[nodiscard]] size_t sensor_count() const;

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Assign a role to a sensor
     * @note MUST be called from main LVGL thread (updates subjects directly)
     *
     * @param klipper_name Full Klipper object name
     * @param role New role assignment
     */
    void set_sensor_role(const std::string& klipper_name, ProbeSensorRole role);

    /**
     * @brief Enable or disable a sensor
     * @note MUST be called from main LVGL thread (updates subjects directly)
     *
     * @param klipper_name Full Klipper object name
     * @param enabled Whether sensor should be monitored
     */
    void set_sensor_enabled(const std::string& klipper_name, bool enabled);

    // ========================================================================
    // State Queries
    // ========================================================================

    /**
     * @brief Get current state for a sensor by role (thread-safe copy)
     *
     * @param role The sensor role to query
     * @return State copy if sensor assigned to role, empty optional otherwise
     */
    [[nodiscard]] std::optional<ProbeSensorState> get_sensor_state(ProbeSensorRole role) const;

    /**
     * @brief Check if a sensor is available (exists and enabled)
     *
     * @param role The sensor role to check
     * @return true if sensor exists, is enabled, and is available in Klipper
     */
    [[nodiscard]] bool is_sensor_available(ProbeSensorRole role) const;

    /**
     * @brief Get last Z probe result for Z_PROBE role
     *
     * @return Last Z result in mm, or 0.0 if no sensor assigned or disabled
     */
    [[nodiscard]] float get_last_z_result() const;

    /**
     * @brief Get Z offset for Z_PROBE role
     *
     * @return Z offset in mm, or 0.0 if no sensor assigned or disabled
     */
    [[nodiscard]] float get_z_offset() const;

    // ========================================================================
    // LVGL Subjects
    // ========================================================================

    /**
     * @brief Get subject for probe triggered state
     * @return Subject (int: -1=no probe, 0=not triggered, 1=triggered)
     */
    [[nodiscard]] lv_subject_t* get_probe_triggered_subject();

    /**
     * @brief Get subject for last Z probe result
     * @return Subject (int: mm x 1000 in microns, -1 if no sensor assigned)
     */
    [[nodiscard]] lv_subject_t* get_probe_last_z_subject();

    /**
     * @brief Get subject for Z offset
     * @return Subject (int: mm x 1000 in microns, -1 if no sensor assigned)
     */
    [[nodiscard]] lv_subject_t* get_probe_z_offset_subject();

    /**
     * @brief Get subject for sensor count (for conditional UI visibility)
     * @return Subject (int: number of discovered sensors)
     */
    [[nodiscard]] lv_subject_t* get_sensor_count_subject();

    /**
     * @brief Enable synchronous mode for testing
     *
     * When enabled, update_from_status() calls update_subjects() synchronously
     * instead of using queue_update().
     */
    void set_sync_mode(bool enabled);

    /// Override the probe type for the primary probe sensor.
    /// Called by PrinterState after printer detection identifies a specific probe type
    /// that can't be inferred from Klipper object names alone (e.g., prtouch_v2 registers
    /// as generic "probe").
    void set_probe_type_override(ProbeSensorType type);

    /**
     * @brief Update subjects on main LVGL thread (called by async callback)
     */
    void update_subjects_on_main_thread();

    friend class ProbeSensorManagerTestAccess;
    friend class ProbeLoadCellTestAccess;

  private:
    ProbeSensorManager();
    ~ProbeSensorManager();

    /**
     * @brief Parse Klipper object name to determine if it's a probe sensor
     *
     * @param klipper_name Full name like "probe", "bltouch", "probe_eddy_current btt"
     * @param[out] sensor_name Extracted short name
     * @param[out] type Detected sensor type
     * @return true if successfully parsed as probe sensor
     */
    /// Probe configs for an objects list, one per physical probe: alias objects
    /// (the generic probe object, an eddy object beside a named scanner) are dropped.
    static std::vector<ProbeSensorConfig>
    probes_in(const std::vector<std::string>& klipper_objects);

    static bool parse_klipper_name(const std::string& klipper_name, std::string& sensor_name,
                                   ProbeSensorType& type);

    /**
     * @brief Update all LVGL subjects from current state
     * @note Internal method - MUST only be called from main LVGL thread
     */
    void update_subjects();

    // Recursive mutex for thread-safe state access
    mutable std::recursive_mutex mutex_;

    SensorCollection<ProbeSensorConfig, ProbeSensorState> sensors_;

    // Test mode: when true, update_from_status() calls update_subjects() synchronously
    bool sync_mode_ = false;

    // LVGL subjects
    bool subjects_initialized_ = false;
    SubjectManager subjects_;
    // Expires deferred subject updates when the subjects are torn down.
    helix::AsyncLifetimeGuard lifetime_;
    lv_subject_t probe_triggered_{};
    lv_subject_t probe_last_z_{};
    lv_subject_t probe_z_offset_{};
    lv_subject_t sensor_count_{};
};

} // namespace helix::sensors
