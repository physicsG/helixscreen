// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sensor_enum_names.h"

#include <string>

namespace helix::sensors {

/// @brief Role assigned to an accelerometer sensor
enum class AccelSensorRole {
    NONE = 0,         ///< Discovered but not assigned to a role
    INPUT_SHAPER = 1, ///< Used for input shaping calibration
};

/// @brief Type of accelerometer hardware
enum class AccelSensorType {
    ADXL345 = 1,  ///< ADXL345 accelerometer
    LIS2DW = 2,   ///< LIS2DW accelerometer
    LIS3DH = 3,   ///< LIS3DH accelerometer
    MPU9250 = 4,  ///< MPU9250 accelerometer
    ICM20948 = 5, ///< ICM20948 accelerometer
};

/// @brief Configuration for an accelerometer sensor
struct AccelSensorConfig {
    std::string klipper_name; ///< Full Klipper name (e.g., "adxl345", "adxl345 bed")
    std::string sensor_name;  ///< Short name (e.g., "adxl345", "bed")
    AccelSensorType type = AccelSensorType::ADXL345;
    AccelSensorRole role = AccelSensorRole::NONE;
    bool enabled = true;

    AccelSensorConfig() = default;

    AccelSensorConfig(std::string klipper_name_, std::string sensor_name_, AccelSensorType type_)
        : klipper_name(std::move(klipper_name_)), sensor_name(std::move(sensor_name_)),
          type(type_) {}
};

/// @brief Runtime state for an accelerometer sensor
struct AccelSensorState {
    bool available = false; ///< Sensor available in current config
};

inline constexpr EnumName<AccelSensorRole> kAccelSensorRoles[] = {
    {AccelSensorRole::NONE, "none", "Unassigned"},
    {AccelSensorRole::INPUT_SHAPER, "input_shaper", "Input Shaper"},
};

inline constexpr EnumName<AccelSensorType> kAccelSensorTypes[] = {
    {AccelSensorType::ADXL345, "adxl345", "ADXL345"},
    {AccelSensorType::LIS2DW, "lis2dw", "LIS2DW"},
    {AccelSensorType::LIS3DH, "lis3dh", "LIS3DH"},
    {AccelSensorType::MPU9250, "mpu9250", "MPU9250"},
    {AccelSensorType::ICM20948, "icm20948", "ICM20948"},
};

[[nodiscard]] inline std::string accel_role_to_string(AccelSensorRole role) {
    return enum_id(kAccelSensorRoles, role);
}

[[nodiscard]] inline AccelSensorRole accel_role_from_string(const std::string& str) {
    return enum_from_id(kAccelSensorRoles, str);
}

[[nodiscard]] inline std::string accel_role_to_display_string(AccelSensorRole role) {
    return enum_display(kAccelSensorRoles, role);
}

[[nodiscard]] inline std::string accel_type_to_string(AccelSensorType type) {
    return enum_id(kAccelSensorTypes, type);
}

[[nodiscard]] inline AccelSensorType accel_type_from_string(const std::string& str) {
    return enum_from_id(kAccelSensorTypes, str);
}

} // namespace helix::sensors
