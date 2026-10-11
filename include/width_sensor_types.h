// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sensor_enum_names.h"

#include <string>

namespace helix::sensors {

/// @brief Role assigned to a width sensor
enum class WidthSensorRole {
    NONE = 0,              ///< Discovered but not assigned to a role
    FLOW_COMPENSATION = 1, ///< Used for flow rate compensation based on filament diameter
};

/// @brief Type of width sensor hardware
enum class WidthSensorType {
    TSL1401CL = 1, ///< TSL1401CL linear array sensor
    HALL = 2,      ///< Hall effect based sensor
};

/// @brief Configuration for a width sensor
struct WidthSensorConfig {
    std::string klipper_name; ///< Full Klipper name (e.g., "tsl1401cl_filament_width_sensor")
    std::string sensor_name;  ///< Short name (e.g., "tsl1401cl")
    WidthSensorType type = WidthSensorType::TSL1401CL;
    WidthSensorRole role = WidthSensorRole::NONE;
    bool enabled = true;

    WidthSensorConfig() = default;

    WidthSensorConfig(std::string klipper_name_, std::string sensor_name_, WidthSensorType type_)
        : klipper_name(std::move(klipper_name_)), sensor_name(std::move(sensor_name_)),
          type(type_) {}
};

/// @brief Runtime state for a width sensor
struct WidthSensorState {
    float diameter = 0.0f;  ///< Measured filament diameter in mm
    float raw_value = 0.0f; ///< Raw sensor value
    bool available = false; ///< Sensor available in current config
};

inline constexpr EnumName<WidthSensorRole> kWidthSensorRoles[] = {
    {WidthSensorRole::NONE, "none", "Unassigned"},
    {WidthSensorRole::FLOW_COMPENSATION, "flow_compensation", "Flow Compensation"},
};

inline constexpr EnumName<WidthSensorType> kWidthSensorTypes[] = {
    {WidthSensorType::TSL1401CL, "tsl1401cl", "TSL1401CL"},
    {WidthSensorType::HALL, "hall", "Hall"},
};

[[nodiscard]] inline std::string width_role_to_string(WidthSensorRole role) {
    return enum_id(kWidthSensorRoles, role);
}

[[nodiscard]] inline WidthSensorRole width_role_from_string(const std::string& str) {
    return enum_from_id(kWidthSensorRoles, str);
}

[[nodiscard]] inline std::string width_role_to_display_string(WidthSensorRole role) {
    return enum_display(kWidthSensorRoles, role);
}

[[nodiscard]] inline std::string width_type_to_string(WidthSensorType type) {
    return enum_id(kWidthSensorTypes, type);
}

[[nodiscard]] inline WidthSensorType width_type_from_string(const std::string& str) {
    return enum_from_id(kWidthSensorTypes, str);
}

} // namespace helix::sensors
