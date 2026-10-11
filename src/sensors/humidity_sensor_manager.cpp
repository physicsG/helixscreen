// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "humidity_sensor_manager.h"

#include "ui_update_queue.h"

#include "spdlog/spdlog.h"
#include "static_subject_registry.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// CRITICAL: Subject updates trigger lv_obj_invalidate() which asserts if called
// during LVGL rendering. WebSocket callbacks run on libhv's event loop thread,
// not the main LVGL thread. Subject updates are deferred to the main thread
// through lifetime_.token().defer() to avoid the "Invalidate area not allowed
// during rendering" assertion, and dropped once deinit_subjects() runs.

namespace helix::sensors {

// ============================================================================
// Singleton
// ============================================================================

HumiditySensorManager& HumiditySensorManager::instance() {
    static HumiditySensorManager instance;
    return instance;
}

HumiditySensorManager::HumiditySensorManager() = default;

HumiditySensorManager::~HumiditySensorManager() = default;

// ============================================================================
// Discovery, Status and Config
// ============================================================================

void HumiditySensorManager::discover(const std::vector<std::string>& klipper_objects) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[HumiditySensorManager] Discovering humidity sensors from {} objects",
                  klipper_objects.size());

    std::vector<HumiditySensorConfig> discovered;
    for (const auto& klipper_name : klipper_objects) {
        std::string sensor_name;
        HumiditySensorType type = HumiditySensorType::BME280;

        if (!parse_klipper_name(klipper_name, sensor_name, type)) {
            continue;
        }

        discovered.emplace_back(klipper_name, sensor_name, type);
        spdlog::debug("[HumiditySensorManager] Discovered sensor: {} (type: {})", sensor_name,
                      humidity_type_to_string(type));
    }
    sensors_.reconcile(std::move(discovered));

    // Update sensor count subject
    if (subjects_initialized_) {
        int new_count = static_cast<int>(sensors_.size());
        lv_subject_set_int(&sensor_count_, new_count);
    }

    spdlog::info("[HumiditySensorManager] Discovered {} humidity sensors", sensors_.size());

    // Auto-assign roles from the sensor name: the first "chamber" sensor gets CHAMBER,
    // the first "dryer" sensor gets DRYER. sensors_ was rebuilt above, so every
    // sensor starts at NONE here and the result depends only on the names.
    if (!sensors_.empty()) {
        bool has_chamber_role = sensors_.find_by_role(HumiditySensorRole::CHAMBER) != nullptr;
        bool has_dryer_role = sensors_.find_by_role(HumiditySensorRole::DRYER) != nullptr;

        for (auto& sensor : sensors_) {
            if (!has_chamber_role && sensor.role == HumiditySensorRole::NONE &&
                sensor.sensor_name.find("chamber") != std::string::npos) {
                sensor.role = HumiditySensorRole::CHAMBER;
                has_chamber_role = true;
                spdlog::info("[HumiditySensorManager] Auto-assigned CHAMBER role to {}",
                             sensor.sensor_name);
            } else if (!has_dryer_role && sensor.role == HumiditySensorRole::NONE &&
                       sensor.sensor_name.find("dryer") != std::string::npos) {
                sensor.role = HumiditySensorRole::DRYER;
                has_dryer_role = true;
                spdlog::info("[HumiditySensorManager] Auto-assigned DRYER role to {}",
                             sensor.sensor_name);
            }
        }
    }

    // Update subjects to reflect new state
    update_subjects();
}

void HumiditySensorManager::update_from_status(const nlohmann::json& status) {
    bool any_changed = false;

    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        for (const auto& sensor : sensors_) {
            const std::string& key = sensor.klipper_name;

            if (!status.contains(key)) {
                continue;
            }

            const auto& sensor_data = status[key];
            auto& state = sensors_.state_at(sensor.klipper_name);
            HumiditySensorState old_state = state;

            // Field-restricted Moonraker subscriptions send null for absent
            // fields; `contains()` returns true for null and .get<float>()
            // would throw type_error.302. Skip via find() + is_number().
            if (auto it = sensor_data.find("humidity");
                it != sensor_data.end() && it->is_number()) {
                state.humidity = it->get<float>();
            }
            if (auto it = sensor_data.find("temperature");
                it != sensor_data.end() && it->is_number()) {
                state.temperature = it->get<float>();
            }
            // Pressure is BME280-only (HTU21D doesn't expose it).
            if (auto it = sensor_data.find("pressure");
                it != sensor_data.end() && it->is_number()) {
                state.pressure = it->get<float>();
            }

            // Check for state change (compare at display precision to avoid log spam)
            if (state.humidity != old_state.humidity ||
                state.temperature != old_state.temperature ||
                state.pressure != old_state.pressure) {
                any_changed = true;
                // Only log when the formatted values actually differ
                if (std::lround(state.humidity * 10) != std::lround(old_state.humidity * 10) ||
                    std::lround(state.temperature * 10) !=
                        std::lround(old_state.temperature * 10) ||
                    std::lround(state.pressure * 10) != std::lround(old_state.pressure * 10)) {
                    spdlog::trace("[HumiditySensorManager] Sensor {} updated: humidity={:.1f}%, "
                                  "temp={:.1f}C, pressure={:.1f}hPa",
                                  sensor.sensor_name, state.humidity, state.temperature,
                                  state.pressure);
                }
            }
        }

        if (any_changed) {
            if (sync_mode_) {
                spdlog::debug("[HumiditySensorManager] sync_mode: updating subjects synchronously");
                update_subjects();
            } else {
                spdlog::trace("[HumiditySensorManager] async_mode: deferring via lifetime token");
                lifetime_.token().defer("HumiditySensorManager::update_from_status", [] {
                    HumiditySensorManager::instance().update_subjects_on_main_thread();
                });
            }
        }
    }
}

void HumiditySensorManager::load_config(const nlohmann::json& config) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[HumiditySensorManager] Loading config");

    if (!sensors_.apply_json(config, humidity_role_from_string)) {
        spdlog::debug("[HumiditySensorManager] No sensors config found");
        return;
    }

    update_subjects();
    spdlog::info("[HumiditySensorManager] Config loaded");
}

nlohmann::json HumiditySensorManager::save_config() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[HumiditySensorManager] Saving config");

    auto config = sensors_.to_json(humidity_role_to_string, humidity_type_to_string);

    spdlog::info("[HumiditySensorManager] Config saved");
    return config;
}

// ============================================================================
// Initialization
// ============================================================================

void HumiditySensorManager::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    spdlog::trace("[HumiditySensorManager] Initializing subjects");

    // Initialize subjects with SubjectManager for automatic cleanup
    // -1 = no sensor assigned, 0+ = humidity x 10
    UI_MANAGED_SUBJECT_INT(chamber_humidity_, -1, "chamber_humidity", subjects_);
    // -1 = no sensor assigned, 0+ = pressure in Pa
    UI_MANAGED_SUBJECT_INT(chamber_pressure_, -1, "chamber_pressure", subjects_);
    // -1 = no sensor assigned, 0+ = humidity x 10
    UI_MANAGED_SUBJECT_INT(dryer_humidity_, -1, "dryer_humidity", subjects_);
    UI_MANAGED_SUBJECT_INT(sensor_count_, 0, "humidity_sensor_count", subjects_);

    subjects_initialized_ = true;

    // Self-register cleanup — ensures deinit runs before lv_deinit()
    StaticSubjectRegistry::instance().register_deinit(
        "HumiditySensorManager", []() { HumiditySensorManager::instance().deinit_subjects(); });

    spdlog::trace("[HumiditySensorManager] Subjects initialized");
}

void HumiditySensorManager::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    lifetime_.invalidate();

    spdlog::trace("[HumiditySensorManager] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
    spdlog::trace("[HumiditySensorManager] Subjects deinitialized");
}

// ============================================================================
// Sensor Queries
// ============================================================================

bool HumiditySensorManager::has_sensors() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return !sensors_.empty();
}

std::vector<HumiditySensorConfig> HumiditySensorManager::get_sensors() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sensors_.configs(); // Return thread-safe copy
}

size_t HumiditySensorManager::sensor_count() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sensors_.size();
}

// ============================================================================
// Configuration
// ============================================================================

void HumiditySensorManager::set_sensor_role(const std::string& klipper_name,
                                            HumiditySensorRole role) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (auto* sensor = sensors_.assign_exclusive_role(klipper_name, role)) {
        spdlog::info("[HumiditySensorManager] Set role for {} to {}", sensor->sensor_name,
                     humidity_role_to_string(role));
        update_subjects();
    }
}

void HumiditySensorManager::set_sensor_enabled(const std::string& klipper_name, bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (auto* sensor = sensors_.find(klipper_name)) {
        sensor->enabled = enabled;
        spdlog::info("[HumiditySensorManager] Set enabled for {} to {}", sensor->sensor_name,
                     enabled);
        update_subjects();
    }
}

// ============================================================================
// State Queries
// ============================================================================

std::optional<HumiditySensorState>
HumiditySensorManager::get_sensor_state(HumiditySensorRole role) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto* state = sensors_.role_state(role);
    return state ? std::optional(*state) : std::nullopt;
}

bool HumiditySensorManager::is_sensor_available(HumiditySensorRole role) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sensors_.live_state(role) != nullptr;
}

// ============================================================================
// LVGL Subjects
// ============================================================================

lv_subject_t* HumiditySensorManager::get_chamber_humidity_subject() {
    return &chamber_humidity_;
}

lv_subject_t* HumiditySensorManager::get_chamber_pressure_subject() {
    return &chamber_pressure_;
}

lv_subject_t* HumiditySensorManager::get_dryer_humidity_subject() {
    return &dryer_humidity_;
}

lv_subject_t* HumiditySensorManager::get_sensor_count_subject() {
    return &sensor_count_;
}

// ============================================================================
// Testing Support
// ============================================================================

void HumiditySensorManager::set_sync_mode(bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sync_mode_ = enabled;
}

void HumiditySensorManager::update_subjects_on_main_thread() {
    update_subjects();
}

// ============================================================================
// Private Helpers
// ============================================================================

bool HumiditySensorManager::parse_klipper_name(const std::string& klipper_name,
                                               std::string& sensor_name,
                                               HumiditySensorType& type) const {
    // Match against the single source-of-truth chip table (humidity_sensor_types.h).
    // Adding a new humidity chip requires only a new table row, not edits here.
    const auto* chip = humidity_chip_for_object(klipper_name);
    if (!chip)
        return false;
    sensor_name = klipper_name.substr(chip->klipper_prefix.size());
    type = chip->type;
    return true;
}

void HumiditySensorManager::update_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    // -1 when no enabled, available sensor holds the role
    const auto* chamber = sensors_.live_state(HumiditySensorRole::CHAMBER);
    const auto* dryer = sensors_.live_state(HumiditySensorRole::DRYER);
    // humidity as % x 10, pressure hPa -> Pa
    lv_subject_set_int(&chamber_humidity_,
                       chamber ? static_cast<int>(chamber->humidity * 10.0f) : -1);
    lv_subject_set_int(&chamber_pressure_,
                       chamber ? static_cast<int>(chamber->pressure * 100.0f) : -1);
    lv_subject_set_int(&dryer_humidity_, dryer ? static_cast<int>(dryer->humidity * 10.0f) : -1);

    spdlog::trace("[HumiditySensorManager] Subjects updated: chamber_humidity={}, "
                  "chamber_pressure={}, dryer_humidity={}",
                  lv_subject_get_int(&chamber_humidity_), lv_subject_get_int(&chamber_pressure_),
                  lv_subject_get_int(&dryer_humidity_));
}

} // namespace helix::sensors
