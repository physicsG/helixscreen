// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sensor_enum_names.h"

#include <string>

namespace helix::sensors {

/// @brief Type of accelerometer hardware
enum class AccelSensorType {
    ADXL345 = 1,  ///< ADXL345 accelerometer
    LIS2DW = 2,   ///< LIS2DW accelerometer
    LIS3DH = 3,   ///< LIS3DH accelerometer
    MPU9250 = 4,  ///< MPU9250 accelerometer
    ICM20948 = 5, ///< ICM20948 accelerometer
};

inline constexpr EnumName<AccelSensorType> kAccelSensorTypes[] = {
    {AccelSensorType::ADXL345, "adxl345", "ADXL345"},
    {AccelSensorType::LIS2DW, "lis2dw", "LIS2DW"},
    {AccelSensorType::LIS3DH, "lis3dh", "LIS3DH"},
    {AccelSensorType::MPU9250, "mpu9250", "MPU9250"},
    {AccelSensorType::ICM20948, "icm20948", "ICM20948"},
};

[[nodiscard]] inline std::string accel_type_to_string(AccelSensorType type) {
    return enum_id(kAccelSensorTypes, type);
}

[[nodiscard]] inline AccelSensorType accel_type_from_string(const std::string& str) {
    return enum_from_id(kAccelSensorTypes, str);
}

} // namespace helix::sensors
