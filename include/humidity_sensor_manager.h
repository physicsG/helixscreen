// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "async_lifetime_guard.h"
#include "humidity_sensor_types.h"
#include "lvgl.h"
#include "sensor_collection.h"
#include "subject_managed_panel.h"

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "hv/json.hpp"

namespace helix::sensors {

/**
 * @brief Manager for humidity sensors (BME280, HTU21D, SHT3X, AHT10/20/20-F)
 *
 * Provides:
 * - Auto-discovery of humidity sensors from Klipper objects list
 * - Role assignment for CHAMBER and DRYER monitoring
 * - Real-time state tracking from Moonraker updates
 * - LVGL subjects for reactive UI binding
 *
 * Thread-safe for state updates from Moonraker callbacks.
 *
 * The set of recognized chips is the single source-of-truth table
 * humidity_sensor_chips() in humidity_sensor_types.h. To support a new
 * humidity chip, add one table row plus one enum value there; discovery,
 * config persistence, and UI labels all derive from that table.
 *
 * Klipper object names (prefix -> chip): "bme280 " (pressure+humidity+temp),
 * "htu21d ", "sht3x ", "aht10 ", "aht20 ", "aht20_f " (humidity+temp).
 *
 * Status JSON format:
 * @code
 * {
 *   "bme280 chamber": {
 *     "humidity": 45.5,
 *     "pressure": 1013.25,
 *     "temperature": 25.3
 *   },
 *   "htu21d dryer": {
 *     "humidity": 20.1,
 *     "temperature": 55.0
 *   }
 * }
 * @endcode
 */
class HumiditySensorManager {
  public:
    /**
     * @brief Get singleton instance
     */
    static HumiditySensorManager& instance();

    // Prevent copying
    HumiditySensorManager(const HumiditySensorManager&) = delete;
    HumiditySensorManager& operator=(const HumiditySensorManager&) = delete;

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
     * @brief Load sensor configuration from JSON
     * @note MUST be called from main LVGL thread (updates subjects directly)
     */
    void load_config(const nlohmann::json& config);

    /// @brief Save configuration to JSON
    [[nodiscard]] nlohmann::json save_config() const;

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
    [[nodiscard]] std::vector<HumiditySensorConfig> get_sensors() const;

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
    void set_sensor_role(const std::string& klipper_name, HumiditySensorRole role);

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
    [[nodiscard]] std::optional<HumiditySensorState>
    get_sensor_state(HumiditySensorRole role) const;

    /**
     * @brief Check if a sensor is available (exists and enabled)
     *
     * @param role The sensor role to check
     * @return true if sensor exists, is enabled, and is available in Klipper
     */
    [[nodiscard]] bool is_sensor_available(HumiditySensorRole role) const;

    // ========================================================================
    // LVGL Subjects
    // ========================================================================

    /**
     * @brief Get subject for chamber humidity
     * @return Subject (int: humidity x 10, -1 if no chamber sensor)
     */
    [[nodiscard]] lv_subject_t* get_chamber_humidity_subject();

    /**
     * @brief Death signal for the subjects this HumiditySensorManager owns.
     *
     * Pass to observe_*() from anything that can outlive this object's
     * deinit_subjects(): that path frees every observer node without bumping
     * the ObserverGuard invalidation epoch, so a guard without the token
     * dereferences a freed observer on its next reset().
     */
    [[nodiscard]] SubjectLifetime get_subjects_lifetime() const {
        return subjects_.get_subjects_lifetime();
    }

    /**
     * @brief Get subject for chamber pressure
     * @return Subject (int: pressure in Pa, -1 if no chamber sensor)
     */
    [[nodiscard]] lv_subject_t* get_chamber_pressure_subject();

    /**
     * @brief Get subject for dryer humidity
     * @return Subject (int: humidity x 10, -1 if no dryer sensor)
     */
    [[nodiscard]] lv_subject_t* get_dryer_humidity_subject();

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

    /**
     * @brief Update subjects on main LVGL thread (called by async callback)
     */
    void update_subjects_on_main_thread();

    friend class HumiditySensorManagerTestAccess;

  private:
    HumiditySensorManager();
    ~HumiditySensorManager();

    /**
     * @brief Parse Klipper object name to determine if it's a humidity sensor
     *
     * Matched against the humidity_sensor_chips() chip table by prefix.
     * @param klipper_name Full name like "bme280 chamber" or "aht10 dryer"
     * @param[out] sensor_name Extracted short name (e.g., "chamber")
     * @param[out] type Detected sensor type
     * @return true if successfully parsed as humidity sensor
     */
    bool parse_klipper_name(const std::string& klipper_name, std::string& sensor_name,
                            HumiditySensorType& type) const;

    /**
     * @brief Update all LVGL subjects from current state
     * @note Internal method - MUST only be called from main LVGL thread
     */
    void update_subjects();

    // Recursive mutex for thread-safe state access
    mutable std::recursive_mutex mutex_;

    SensorCollection<HumiditySensorConfig, HumiditySensorState> sensors_;

    // Test mode: when true, update_from_status() calls update_subjects() synchronously
    bool sync_mode_ = false;

    // LVGL subjects
    bool subjects_initialized_ = false;
    SubjectManager subjects_;
    // Expires deferred subject updates when the subjects are torn down.
    helix::AsyncLifetimeGuard lifetime_;
    lv_subject_t chamber_humidity_{};
    lv_subject_t chamber_pressure_{};
    lv_subject_t dryer_humidity_{};
    lv_subject_t sensor_count_{};
};

} // namespace helix::sensors
