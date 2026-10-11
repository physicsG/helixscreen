// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "width_sensor_manager.h"

#include "ui_update_queue.h"

#include "config.h"
#include "spdlog/spdlog.h"
#include "static_subject_registry.h"

#include <algorithm>

// CRITICAL: Subject updates trigger lv_obj_invalidate() which asserts if called
// during LVGL rendering. WebSocket callbacks run on libhv's event loop thread,
// not the main LVGL thread. Subject updates are deferred to the main thread
// through lifetime_.token().defer() to avoid the "Invalidate area not allowed
// during rendering" assertion, and dropped once deinit_subjects() runs.

namespace helix::sensors {

// ============================================================================
// Singleton
// ============================================================================

WidthSensorManager& WidthSensorManager::instance() {
    static WidthSensorManager instance;
    return instance;
}

WidthSensorManager::WidthSensorManager() = default;

WidthSensorManager::~WidthSensorManager() = default;

// ============================================================================
// Discovery, Status and Config
// ============================================================================

void WidthSensorManager::discover(const std::vector<std::string>& klipper_objects) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[WidthSensorManager] Discovering width sensors from {} objects",
                  klipper_objects.size());

    std::vector<WidthSensorConfig> discovered;
    for (const auto& klipper_name : klipper_objects) {
        std::string sensor_name;
        WidthSensorType type = WidthSensorType::TSL1401CL;

        if (!parse_klipper_name(klipper_name, sensor_name, type)) {
            continue;
        }

        discovered.emplace_back(klipper_name, sensor_name, type);
        spdlog::debug("[WidthSensorManager] Discovered sensor: {} (type: {})", sensor_name,
                      width_type_to_string(type));
    }
    sensors_.reconcile(std::move(discovered));

    // Auto-assign first discovered sensor to FLOW_COMPENSATION role as a default.
    // This ensures the diameter subject gets populated for first-time users.
    // User-saved config from load_config_from_file() is applied AFTER discover()
    // and will override this default if the user explicitly set a different role.
    if (!sensors_.empty()) {
        sensors_.front().role = WidthSensorRole::FLOW_COMPENSATION;
        spdlog::debug("[WidthSensorManager] Auto-assigned {} to FLOW_COMPENSATION role (default)",
                      sensors_.front().sensor_name);
    }

    // Update sensor count subject
    if (subjects_initialized_) {
        lv_subject_set_int(&sensor_count_, static_cast<int>(sensors_.size()));
    }

    spdlog::info("[WidthSensorManager] Discovered {} width sensors", sensors_.size());

    // Update subjects to reflect new state
    update_subjects();
}

void WidthSensorManager::update_from_status(const nlohmann::json& status) {
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
            WidthSensorState old_state = state;

            // Field-restricted Moonraker subscriptions can send null for absent
            // fields; guard with find() + is_number() to avoid type_error.302.
            if (auto it = sensor_data.find("Diameter");
                it != sensor_data.end() && it->is_number()) {
                state.diameter = it->get<float>();
            }
            if (auto it = sensor_data.find("Raw"); it != sensor_data.end() && it->is_number()) {
                state.raw_value = it->get<float>();
            }

            // Check for state change
            if (state.diameter != old_state.diameter || state.raw_value != old_state.raw_value) {
                any_changed = true;
                spdlog::debug("[WidthSensorManager] Sensor {} updated: diameter={:.3f}mm, raw={}",
                              sensor.sensor_name, state.diameter, state.raw_value);
            }
        }

        if (any_changed) {
            if (sync_mode_) {
                spdlog::debug("[WidthSensorManager] sync_mode: updating subjects synchronously");
                update_subjects();
            } else {
                spdlog::debug("[WidthSensorManager] async_mode: deferring via lifetime token");
                lifetime_.token().defer("WidthSensorManager::update_from_status", [] {
                    WidthSensorManager::instance().update_subjects_on_main_thread();
                });
            }
        }
    }
}

void WidthSensorManager::load_config(const nlohmann::json& config) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[WidthSensorManager] Loading config");

    if (!sensors_.apply_json(config, width_role_from_string)) {
        spdlog::debug("[WidthSensorManager] No sensors config found");
        return;
    }

    update_subjects();
    spdlog::info("[WidthSensorManager] Config loaded");
}

nlohmann::json WidthSensorManager::save_config() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[WidthSensorManager] Saving config");

    auto config = sensors_.to_json(width_role_to_string, width_type_to_string);

    spdlog::info("[WidthSensorManager] Config saved");
    return config;
}

void WidthSensorManager::load_config_from_file() {
    spdlog::debug("[WidthSensorManager] Loading config from file");

    Config* config = Config::get_instance();

    // Reuse load_config() to avoid deserialization drift (mirrors save_config_to_file)
    std::string base_path = config->df() + "width_sensors";
    const nlohmann::json* config_json = config->try_get_json(base_path);
    if (config_json != nullptr) {
        load_config(*config_json);
    } else {
        spdlog::debug("[WidthSensorManager] No saved config found");
    }

    spdlog::info("[WidthSensorManager] Config loaded from file");
}

void WidthSensorManager::save_config_to_file() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[WidthSensorManager] Saving config to file");

    Config* config = Config::get_instance();

    // Build path using default printer prefix
    std::string base_path = config->df() + "width_sensors";

    // Reuse save_config() to avoid serialization drift
    config->get_json(base_path) = save_config();
    config->save();

    spdlog::info("[WidthSensorManager] Config saved to file");
}

// ============================================================================
// Initialization
// ============================================================================

void WidthSensorManager::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    spdlog::trace("[WidthSensorManager] Initializing subjects");

    // Initialize subjects with SubjectManager for automatic cleanup
    // -1 = no sensor assigned, 0+ = diameter in mm * 1000
    UI_MANAGED_SUBJECT_INT(diameter_, -1, "filament_width_diameter", subjects_);
    UI_MANAGED_SUBJECT_INT(sensor_count_, 0, "width_sensor_count", subjects_);
    // Text subject for display (formatted as "1.75mm" or "--")
    UI_MANAGED_SUBJECT_STRING(diameter_text_, diameter_text_buf_, "--", "filament_diameter_text",
                              subjects_);

    subjects_initialized_ = true;

    // Self-register cleanup — ensures deinit runs before lv_deinit()
    StaticSubjectRegistry::instance().register_deinit(
        "WidthSensorManager", []() { WidthSensorManager::instance().deinit_subjects(); });

    spdlog::trace("[WidthSensorManager] Subjects initialized");
}

void WidthSensorManager::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    lifetime_.invalidate();

    spdlog::trace("[WidthSensorManager] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
    spdlog::trace("[WidthSensorManager] Subjects deinitialized");
}

// ============================================================================
// Sensor Queries
// ============================================================================

bool WidthSensorManager::has_sensors() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return !sensors_.empty();
}

std::vector<WidthSensorConfig> WidthSensorManager::get_sensors() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sensors_.configs(); // Return thread-safe copy
}

size_t WidthSensorManager::sensor_count() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sensors_.size();
}

// ============================================================================
// Configuration
// ============================================================================

void WidthSensorManager::set_sensor_role(const std::string& klipper_name, WidthSensorRole role) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (auto* sensor = sensors_.assign_exclusive_role(klipper_name, role)) {
        spdlog::info("[WidthSensorManager] Set role for {} to {}", sensor->sensor_name,
                     width_role_to_string(role));
        update_subjects();
    }
}

void WidthSensorManager::set_sensor_enabled(const std::string& klipper_name, bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (auto* sensor = sensors_.find(klipper_name)) {
        sensor->enabled = enabled;
        spdlog::info("[WidthSensorManager] Set enabled for {} to {}", sensor->sensor_name, enabled);
        update_subjects();
    }
}

// ============================================================================
// State Queries
// ============================================================================

std::optional<WidthSensorState> WidthSensorManager::get_sensor_state(WidthSensorRole role) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto* state = sensors_.role_state(role);
    return state ? std::optional(*state) : std::nullopt;
}

bool WidthSensorManager::is_sensor_available(WidthSensorRole role) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sensors_.live_state(role) != nullptr;
}

float WidthSensorManager::get_flow_compensation_diameter() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto* state = sensors_.live_state(WidthSensorRole::FLOW_COMPENSATION);
    return state ? state->diameter : 0.0f;
}

// ============================================================================
// LVGL Subjects
// ============================================================================

lv_subject_t* WidthSensorManager::get_diameter_subject() {
    return &diameter_;
}

lv_subject_t* WidthSensorManager::get_sensor_count_subject() {
    return &sensor_count_;
}

lv_subject_t* WidthSensorManager::get_diameter_text_subject() {
    return &diameter_text_;
}

// ============================================================================
// Testing Support
// ============================================================================

void WidthSensorManager::set_sync_mode(bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sync_mode_ = enabled;
}

void WidthSensorManager::update_subjects_on_main_thread() {
    update_subjects();
}

// ============================================================================
// Private Helpers
// ============================================================================

bool WidthSensorManager::parse_klipper_name(const std::string& klipper_name,
                                            std::string& sensor_name, WidthSensorType& type) const {
    const std::string tsl_name = "tsl1401cl_filament_width_sensor";
    const std::string hall_name = "hall_filament_width_sensor";

    if (klipper_name == tsl_name) {
        sensor_name = "tsl1401cl";
        type = WidthSensorType::TSL1401CL;
        return true;
    }

    if (klipper_name == hall_name) {
        sensor_name = "hall";
        type = WidthSensorType::HALL;
        return true;
    }

    return false;
}

void WidthSensorManager::update_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    // mm * 1000; -1 when no enabled, available sensor holds the role
    const auto* state = sensors_.live_state(WidthSensorRole::FLOW_COMPENSATION);
    int diameter = state ? static_cast<int>(state->diameter * 1000.0f) : -1;
    lv_subject_set_int(&diameter_, diameter);

    // Text formatting (diameter_text_) handled by UI-layer observer in WidthSensorWidget

    spdlog::trace("[WidthSensorManager] Subjects updated: diameter={}", diameter);
}

} // namespace helix::sensors
