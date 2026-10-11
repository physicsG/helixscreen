// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sensor_enum_names.h"

#include <optional>
#include <string>

namespace helix::sensors {

/// @brief Role assigned to a load cell (auto-categorized during discovery)
enum class LoadCellRole {
    NONE = 0,         ///< Discovered but not assigned to a role
    SPOOL_WEIGHT = 1, ///< Spool weight
};

/// @brief Configuration for a load cell
struct LoadCellConfig {
    std::string klipper_name;               ///< Full Klipper name (e.g., "load_cell spool_weight")
    std::string sensor_name;                ///< Short name (e.g., "spool_weight")
    std::string display_name;               ///< Pretty name (e.g., "Spool Weight")
    LoadCellRole role = LoadCellRole::NONE; ///< Auto-assigned during discovery
    int priority = 100;                     ///< Lower = shown first

    LoadCellConfig() = default;

    LoadCellConfig(std::string klipper_name_, std::string sensor_name_, std::string display_name_)
        : klipper_name(std::move(klipper_name_)), sensor_name(std::move(sensor_name_)),
          display_name(std::move(display_name_)) {}
};

/// @brief Runtime state for a load cell
struct LoadCellState {
    std::optional<float> force_g = std::nullopt; ///< Force in grams
    bool available = false;                      ///< Sensor available in current config
};

inline constexpr EnumName<LoadCellRole> kLoadCellRoles[] = {
    {LoadCellRole::NONE, "none", "Unassigned"},
    {LoadCellRole::SPOOL_WEIGHT, "spool_weight", "Spool Weight"},
};

[[nodiscard]] inline std::string load_cell_role_to_string(LoadCellRole role) {
    return enum_id(kLoadCellRoles, role);
}

} // namespace helix::sensors
