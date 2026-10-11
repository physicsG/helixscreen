// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sensor_enum_names.h"

#include <chrono>
#include <string>

namespace helix {

/// How long a runout-role sensor must stay clear -- while a job holds the machine
/// and an AMS backend is present -- before the removal toast fires.
///
/// A tool change drags filament off the toolhead sensor and feeds the next lane
/// past it, which is identical to a runout at the edge and tells them apart only
/// by duration: a swap leaves the sensor clear for 26-33s and then refills, a
/// runout never refills. Anything shorter than a swap reports every tool change
/// as a runout. Detection and the runout subjects still fire on the edge; only
/// the toast waits.
constexpr std::chrono::seconds RUNOUT_TOAST_DWELL{45};

/**
 * @brief Role that a filament sensor can be assigned to.
 *
 * Each role represents a specific position in the filament path:
 * - RUNOUT: Detects filament presence anywhere in the path (triggers pause on runout)
 * - TOOLHEAD: Near the hotend, verifies filament reached nozzle during load
 * - ENTRY: At filament entry point, detects when filament is first inserted
 */
enum class FilamentSensorRole {
    NONE = 0,     ///< Sensor discovered but not assigned to a role
    RUNOUT = 1,   ///< Primary runout detection sensor
    TOOLHEAD = 2, ///< Toolhead/nozzle proximity sensor
    ENTRY = 3,    ///< Entry point detection sensor
    Z_PROBE = 10  ///< Z probing sensor (maps to Klipper "probe" object)
};

/**
 * @brief Type of filament sensor hardware.
 *
 * Determines what data is available from the sensor:
 * - SWITCH: Simple binary state (filament detected yes/no)
 * - MOTION: Encoder-based, provides motion activity data for jam detection
 */
enum class FilamentSensorType {
    SWITCH, ///< filament_switch_sensor in Klipper
    MOTION  ///< filament_motion_sensor in Klipper (encoder-based)
};

/**
 * @brief User configuration for a single filament sensor.
 *
 * Stored in settings.json and loaded at startup.
 */
struct FilamentSensorConfig {
    std::string klipper_name; ///< Full Klipper object name, e.g. "filament_switch_sensor fsensor"
    std::string sensor_name;  ///< Short name extracted from klipper_name, e.g. "fsensor"
    FilamentSensorRole role;  ///< User-assigned role
    FilamentSensorType type;  ///< Type of sensor (switch or motion)
    bool enabled;             ///< Whether this sensor is actively monitored
    /// AMS slot / toolhead this sensor watches, from the "lane" config key. -1
    /// leaves it to the sensor name (see lane_index_for_sensor).
    int lane = -1;

    FilamentSensorConfig()
        : role(FilamentSensorRole::NONE), type(FilamentSensorType::SWITCH), enabled(true) {}

    FilamentSensorConfig(const std::string& klipper_name_, const std::string& sensor_name_,
                         FilamentSensorType type_)
        : klipper_name(klipper_name_), sensor_name(sensor_name_), role(FilamentSensorRole::NONE),
          type(type_), enabled(true) {}
};

/**
 * @brief Current runtime state of a filament sensor.
 *
 * Updated from Moonraker WebSocket notifications.
 */
struct FilamentSensorState {
    bool filament_detected; ///< Whether filament is currently detected
    bool enabled;           ///< Klipper-level enabled state (motion sensors)
    int detection_count;    ///< Motion sensors: cumulative detection events
    bool available;         ///< Whether the sensor exists in current Klipper config
    bool reported;          ///< A status frame for it has arrived since discovery

    FilamentSensorState()
        : filament_detected(true), enabled(true), detection_count(0), available(false),
          reported(false) {}
    // filament_detected defaults to true ("present until proven empty"): the runout
    // queries ignore the startup grace period and can run before Moonraker's first
    // status frame, which must never read as a runout.
};

inline constexpr sensors::EnumName<FilamentSensorRole> kFilamentSensorRoles[] = {
    {FilamentSensorRole::NONE, "none", "Unassigned"},
    {FilamentSensorRole::RUNOUT, "runout", "Runout Sensor"},
    {FilamentSensorRole::TOOLHEAD, "toolhead", "Toolhead Sensor"},
    {FilamentSensorRole::ENTRY, "entry", "Entry Sensor"},
    {FilamentSensorRole::Z_PROBE, "z_probe", "Z Probe"},
};

inline constexpr sensors::EnumName<FilamentSensorType> kFilamentSensorTypes[] = {
    {FilamentSensorType::SWITCH, "switch", "Switch"},
    {FilamentSensorType::MOTION, "motion", "Motion"},
};

inline const char* role_to_display_string(FilamentSensorRole role) {
    return sensors::enum_display(kFilamentSensorRoles, role);
}

inline const char* role_to_config_string(FilamentSensorRole role) {
    return sensors::enum_id(kFilamentSensorRoles, role);
}

inline FilamentSensorRole role_from_config_string(const std::string& str) {
    return sensors::enum_from_id(kFilamentSensorRoles, str);
}

inline const char* type_to_config_string(FilamentSensorType type) {
    return sensors::enum_id(kFilamentSensorTypes, type);
}

inline FilamentSensorType type_from_config_string(const std::string& str) {
    return sensors::enum_from_id(kFilamentSensorTypes, str);
}

} // namespace helix
