// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "probe_sensor_manager.h"

#include "ui_update_queue.h"

#include "config.h"
#include "spdlog/spdlog.h"
#include "static_subject_registry.h"
#include "text_io.h"

#include <algorithm>
#include <set>

// CRITICAL: Subject updates trigger lv_obj_invalidate() which asserts if called
// during LVGL rendering. WebSocket callbacks run on libhv's event loop thread,
// not the main LVGL thread. Subject updates are deferred to the main thread
// through lifetime_.token().defer() to avoid the "Invalidate area not allowed
// during rendering" assertion, and dropped once deinit_subjects() runs.

namespace helix::sensors {

// ============================================================================
// Singleton
// ============================================================================

ProbeSensorManager& ProbeSensorManager::instance() {
    static ProbeSensorManager instance;
    return instance;
}

ProbeSensorManager::ProbeSensorManager() = default;

ProbeSensorManager::~ProbeSensorManager() = default;

// ============================================================================
// Discovery, Status and Config
// ============================================================================

void ProbeSensorManager::discover(const std::vector<std::string>& klipper_objects) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[ProbeSensorManager] Discovering probe sensors from {} objects",
                  klipper_objects.size());

    sensors_.reconcile(probes_in(klipper_objects));
    for (const auto& sensor : sensors_) {
        spdlog::debug("[ProbeSensorManager] Discovered sensor: {} (type: {})", sensor.sensor_name,
                      probe_type_to_string(sensor.type));
    }

    // Post-discovery refinement: upgrade STANDARD probes to KLICKY when
    // characteristic Klicky macros are present in the objects list.
    // Klicky probes register as a plain [probe] but include deploy/dock macros.
    bool has_standard_probe = std::any_of(sensors_.begin(), sensors_.end(), [](const auto& s) {
        return s.type == ProbeSensorType::STANDARD;
    });

    if (has_standard_probe) {
        // Build a set of macro names from gcode_macro entries
        const std::string macro_prefix = "gcode_macro ";
        std::set<std::string> macros;
        for (const auto& obj : klipper_objects) {
            if (obj.rfind(macro_prefix, 0) == 0 && obj.size() > macro_prefix.size()) {
                macros.insert(obj.substr(macro_prefix.size()));
            }
        }

        // Check for Klicky macro pairs
        bool is_klicky = (macros.count("ATTACH_PROBE") && macros.count("DOCK_PROBE")) ||
                         (macros.count("_Probe_Deploy") && macros.count("_Probe_Stow"));

        if (is_klicky) {
            for (auto& sensor : sensors_) {
                if (sensor.type == ProbeSensorType::STANDARD) {
                    spdlog::debug("[ProbeSensorManager] Upgrading standard probe '{}' to "
                                  "KLICKY (deploy/dock macros present)",
                                  sensor.sensor_name);
                    sensor.type = ProbeSensorType::KLICKY;
                }
            }
        }
    }

    // Update sensor count subject
    if (subjects_initialized_) {
        lv_subject_set_int(&sensor_count_, static_cast<int>(sensors_.size()));
    }

    spdlog::info("[ProbeSensorManager] Discovered {} probe sensors", sensors_.size());

    // Update subjects to reflect new state
    update_subjects();
}

void ProbeSensorManager::discover_from_config(const nlohmann::json& config_keys) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Seed z_offset from configfile for probe sections whose runtime status
    // may return null (e.g., flashforge_loadcell).  The configfile always has
    // the persisted value.
    for (const auto& sensor : sensors_) {
        if (!config_keys.contains(sensor.klipper_name)) {
            continue;
        }
        const auto& section = config_keys[sensor.klipper_name];
        if (!section.contains("z_offset")) {
            continue;
        }

        // Config values are strings in configfile.config
        float z_offset = 0.0f;
        const auto& val = section["z_offset"];
        if (val.is_string()) {
            const auto parsed = helix::text_io::parse_leading<float>(val.get<std::string>());
            if (!parsed) {
                spdlog::debug("[ProbeSensorManager] Invalid z_offset value for {}: not a number",
                              sensor.klipper_name);
                continue;
            }
            z_offset = *parsed;
        } else if (val.is_number()) {
            z_offset = val.get<float>();
        } else {
            continue;
        }

        sensors_.state_at(sensor.klipper_name).z_offset = z_offset;
        spdlog::debug("[ProbeSensorManager] Seeded z_offset={:.3f}mm from config for {}", z_offset,
                      sensor.sensor_name);
    }

    update_subjects();
}

namespace {

// Where each probe type publishes the keys this manager reads, per upstream
// source (table in docs/devel/SENSOR_MANAGEMENT.md). The Cartographer plugin
// nests last_z_result per mode on its own object; the flat keys live on the
// probe object it registers.
const std::string& status_object(const ProbeSensorConfig& probe) {
    static const std::string probe_object = "probe";
    return probe.type == ProbeSensorType::CARTOGRAPHER ? probe_object : probe.klipper_name;
}

// last_query is the result of the last QUERY_PROBE. Beacon publishes none, and
// probe_eddy_current never sets it because it rejects QUERY_PROBE.
bool publishes_last_query(ProbeSensorType type) {
    return type != ProbeSensorType::BEACON && type != ProbeSensorType::EDDY_CURRENT;
}

} // namespace

void ProbeSensorManager::update_from_status(const nlohmann::json& status) {
    bool any_changed = false;

    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        for (const auto& sensor : sensors_) {
            const std::string& key = status_object(sensor);

            if (!status.contains(key)) {
                continue;
            }

            const auto& sensor_data = status[key];
            auto& state = sensors_.state_at(sensor.klipper_name);
            ProbeSensorState old_state = state;

            // A null or absent field keeps the previous value. Klipper answers a
            // requested key its module lacks with null, so the configfile-seeded
            // z_offset survives on mainline, where no probe module publishes it.
            // The Creality K1/K2 and QIDI forks do, and K1's Z_OFFSET_APPLY_PROBE
            // changes it live.
            const auto z = sensor_data.find("last_z_result");
            if (z != sensor_data.end() && z->is_number()) {
                state.last_z_result = z->get<float>();
            }
            const auto offset = sensor_data.find("z_offset");
            if (offset != sensor_data.end() && offset->is_number()) {
                state.z_offset = offset->get<float>();
            }
            // Klipper publishes a bool, Cartographer an int.
            const auto query = sensor_data.find("last_query");
            if (query != sensor_data.end() && (query->is_boolean() || query->is_number())) {
                state.triggered = query->is_boolean() ? query->get<bool>() : query->get<int>() != 0;
            }

            if (state.last_z_result != old_state.last_z_result ||
                state.triggered != old_state.triggered || state.z_offset != old_state.z_offset) {
                any_changed = true;
                spdlog::debug("[ProbeSensorManager] Sensor {} updated: last_z_result={:.3f}mm, "
                              "last_query={}, z_offset={:.3f}mm",
                              sensor.sensor_name, state.last_z_result, state.triggered,
                              state.z_offset);
            }
        }

        if (any_changed) {
            if (sync_mode_) {
                spdlog::debug("[ProbeSensorManager] sync_mode: updating subjects synchronously");
                update_subjects();
            } else {
                spdlog::debug("[ProbeSensorManager] async_mode: deferring via lifetime token");
                lifetime_.token().defer("ProbeSensorManager::update_from_status", [] {
                    ProbeSensorManager::instance().update_subjects_on_main_thread();
                });
            }
        }
    }
}

std::vector<ProbeSensorConfig>
ProbeSensorManager::probes_in(const std::vector<std::string>& klipper_objects) {
    std::vector<ProbeSensorConfig> probes;
    for (const auto& klipper_name : klipper_objects) {
        std::string sensor_name;
        ProbeSensorType type = ProbeSensorType::STANDARD;
        if (parse_klipper_name(klipper_name, sensor_name, type)) {
            probes.emplace_back(klipper_name, sensor_name, type);
        }
    }

    // One physical probe can register several objects: Klipper's probe modules,
    // Beacon and Cartographer all also register the generic probe object. Keep
    // the most specific object only, so a single probe reads as one sensor and
    // is subscribed once. An eddy object beside a named scanner is dropped too.
    const auto has_type = [&probes](ProbeSensorType t) {
        return std::any_of(probes.begin(), probes.end(),
                           [t](const auto& p) { return p.type == t; });
    };
    const bool has_named_scanner =
        has_type(ProbeSensorType::CARTOGRAPHER) || has_type(ProbeSensorType::BEACON);
    const bool has_specific = std::any_of(probes.begin(), probes.end(), [](const auto& p) {
        return p.type != ProbeSensorType::STANDARD;
    });
    probes.erase(std::remove_if(probes.begin(), probes.end(),
                                [&](const auto& p) {
                                    return (has_specific && p.type == ProbeSensorType::STANDARD) ||
                                           (has_named_scanner &&
                                            p.type == ProbeSensorType::EDDY_CURRENT);
                                }),
                 probes.end());
    return probes;
}

nlohmann::json
ProbeSensorManager::required_status_objects(const std::vector<std::string>& klipper_objects) {
    nlohmann::json objects = nlohmann::json::object();
    for (const auto& probe : probes_in(klipper_objects)) {
        objects[status_object(probe)] =
            publishes_last_query(probe.type)
                ? nlohmann::json::array({"last_query", "last_z_result", "z_offset"})
                : nlohmann::json::array({"last_z_result", "z_offset"});
    }
    return objects;
}

void ProbeSensorManager::load_config(const nlohmann::json& config) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[ProbeSensorManager] Loading config");

    if (!sensors_.apply_json(config, probe_role_from_string)) {
        spdlog::debug("[ProbeSensorManager] No sensors config found");
        return;
    }

    update_subjects();
    spdlog::info("[ProbeSensorManager] Config loaded");
}

nlohmann::json ProbeSensorManager::save_config() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[ProbeSensorManager] Saving config");

    auto config = sensors_.to_json(probe_role_to_string, probe_type_to_string);

    spdlog::info("[ProbeSensorManager] Config saved");
    return config;
}

void ProbeSensorManager::load_config_from_file() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[ProbeSensorManager] Loading config from file");

    Config* cfg = Config::get_instance();

    std::string base_path = cfg->df() + "probe_sensors";

    if (const json* saved = cfg->try_get_json(base_path)) {
        sensors_.apply_json(*saved, probe_role_from_string);
    }

    // Auto-assign Z_PROBE role when exactly one probe exists and no role was
    // loaded from config.  This is the common case — single probe is the Z probe.
    if (sensors_.size() == 1 && !sensors_.find_by_role(ProbeSensorRole::Z_PROBE)) {
        sensors_[0].role = ProbeSensorRole::Z_PROBE;
        spdlog::info("[ProbeSensorManager] Auto-assigned Z_PROBE role to '{}'",
                     sensors_[0].sensor_name);
    }

    update_subjects();
    spdlog::debug("[ProbeSensorManager] Config loaded from file");
}

void ProbeSensorManager::save_config_to_file() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    spdlog::debug("[ProbeSensorManager] Saving config to file");

    Config* cfg = Config::get_instance();

    std::string base_path = cfg->df() + "probe_sensors";

    cfg->get_json(base_path) = save_config();
    cfg->save();
    spdlog::info("[ProbeSensorManager] Config saved to file");
}

// ============================================================================
// Initialization
// ============================================================================

void ProbeSensorManager::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    spdlog::trace("[ProbeSensorManager] Initializing subjects");

    // Initialize subjects with SubjectManager for automatic cleanup
    // -1 = no sensor assigned
    UI_MANAGED_SUBJECT_INT(probe_triggered_, -1, "probe_triggered", subjects_);
    UI_MANAGED_SUBJECT_INT(probe_last_z_, -1, "probe_last_z", subjects_);
    UI_MANAGED_SUBJECT_INT(probe_z_offset_, -1, "probe_z_offset", subjects_);
    UI_MANAGED_SUBJECT_INT(sensor_count_, 0, "probe_count", subjects_);

    subjects_initialized_ = true;

    // Self-register cleanup — ensures deinit runs before lv_deinit()
    StaticSubjectRegistry::instance().register_deinit(
        "ProbeSensorManager", []() { ProbeSensorManager::instance().deinit_subjects(); });

    spdlog::trace("[ProbeSensorManager] Subjects initialized");
}

void ProbeSensorManager::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    lifetime_.invalidate();

    spdlog::trace("[ProbeSensorManager] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
    spdlog::trace("[ProbeSensorManager] Subjects deinitialized");
}

// ============================================================================
// Sensor Queries
// ============================================================================

bool ProbeSensorManager::has_sensors() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return !sensors_.empty();
}

std::vector<ProbeSensorConfig> ProbeSensorManager::get_sensors() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sensors_.configs(); // Return thread-safe copy
}

size_t ProbeSensorManager::sensor_count() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sensors_.size();
}

// ============================================================================
// Configuration
// ============================================================================

void ProbeSensorManager::set_sensor_role(const std::string& klipper_name, ProbeSensorRole role) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (auto* sensor = sensors_.assign_exclusive_role(klipper_name, role)) {
        spdlog::info("[ProbeSensorManager] Set role for {} to {}", sensor->sensor_name,
                     probe_role_to_string(role));
        update_subjects();
    }
}

void ProbeSensorManager::set_sensor_enabled(const std::string& klipper_name, bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (auto* sensor = sensors_.find(klipper_name)) {
        sensor->enabled = enabled;
        spdlog::info("[ProbeSensorManager] Set enabled for {} to {}", sensor->sensor_name, enabled);
        update_subjects();
    }
}

// ============================================================================
// State Queries
// ============================================================================

std::optional<ProbeSensorState> ProbeSensorManager::get_sensor_state(ProbeSensorRole role) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto* state = sensors_.role_state(role);
    return state ? std::optional(*state) : std::nullopt;
}

bool ProbeSensorManager::is_sensor_available(ProbeSensorRole role) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sensors_.live_state(role) != nullptr;
}

float ProbeSensorManager::get_last_z_result() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto* state = sensors_.live_state(ProbeSensorRole::Z_PROBE);
    return state ? state->last_z_result : 0.0f;
}

float ProbeSensorManager::get_z_offset() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto* state = sensors_.live_state(ProbeSensorRole::Z_PROBE);
    return state ? state->z_offset : 0.0f;
}

// ============================================================================
// LVGL Subjects
// ============================================================================

lv_subject_t* ProbeSensorManager::get_probe_triggered_subject() {
    return &probe_triggered_;
}

lv_subject_t* ProbeSensorManager::get_probe_last_z_subject() {
    return &probe_last_z_;
}

lv_subject_t* ProbeSensorManager::get_probe_z_offset_subject() {
    return &probe_z_offset_;
}

lv_subject_t* ProbeSensorManager::get_sensor_count_subject() {
    return &sensor_count_;
}

// ============================================================================
// Testing Support
// ============================================================================

void ProbeSensorManager::set_sync_mode(bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sync_mode_ = enabled;
}

void ProbeSensorManager::update_subjects_on_main_thread() {
    update_subjects();
}

// ============================================================================
// Private Helpers
// ============================================================================

bool ProbeSensorManager::parse_klipper_name(const std::string& klipper_name,
                                            std::string& sensor_name, ProbeSensorType& type) {
    // Cartographer 3D scanning/contact probe
    if (klipper_name == "cartographer") {
        sensor_name = "cartographer";
        type = ProbeSensorType::CARTOGRAPHER;
        return true;
    }

    // Beacon eddy current probe
    if (klipper_name == "beacon") {
        sensor_name = "beacon";
        type = ProbeSensorType::BEACON;
        return true;
    }

    // Standard probe
    if (klipper_name == "probe") {
        sensor_name = "probe";
        type = ProbeSensorType::STANDARD;
        return true;
    }

    // BLTouch
    if (klipper_name == "bltouch") {
        sensor_name = "bltouch";
        type = ProbeSensorType::BLTOUCH;
        return true;
    }

    // Smart Effector
    if (klipper_name == "smart_effector") {
        sensor_name = "smart_effector";
        type = ProbeSensorType::SMART_EFFECTOR;
        return true;
    }

    // Eddy current probe: "probe_eddy_current <name>"
    const std::string eddy_prefix = "probe_eddy_current ";
    if (klipper_name.rfind(eddy_prefix, 0) == 0 && klipper_name.size() > eddy_prefix.size()) {
        sensor_name = klipper_name.substr(eddy_prefix.size());
        type = ProbeSensorType::EDDY_CURRENT;
        return true;
    }

    return false;
}

void ProbeSensorManager::set_probe_type_override(ProbeSensorType type) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    for (auto& sensor : sensors_) {
        if (sensor.type == ProbeSensorType::STANDARD) {
            spdlog::info("[ProbeSensorManager] Overriding probe type from STANDARD to {} "
                         "(printer database)",
                         probe_type_to_string(type));
            sensor.type = type;
            update_subjects();
            return;
        }
    }

    spdlog::debug("[ProbeSensorManager] No STANDARD probe to override (may already be typed)");
}

void ProbeSensorManager::update_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    // -1 when no enabled, available sensor holds Z_PROBE; lengths in microns
    const auto* state = sensors_.live_state(ProbeSensorRole::Z_PROBE);
    lv_subject_set_int(&probe_triggered_, state ? (state->triggered ? 1 : 0) : -1);
    lv_subject_set_int(&probe_last_z_,
                       state ? static_cast<int>(state->last_z_result * 1000.0f) : -1);
    lv_subject_set_int(&probe_z_offset_, state ? static_cast<int>(state->z_offset * 1000.0f) : -1);

    spdlog::trace("[ProbeSensorManager] Subjects updated: triggered={}, last_z={}, z_offset={}",
                  lv_subject_get_int(&probe_triggered_), lv_subject_get_int(&probe_last_z_),
                  lv_subject_get_int(&probe_z_offset_));
}

} // namespace helix::sensors
