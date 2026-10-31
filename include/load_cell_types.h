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

/// @brief Runtime state for a load cell
struct LoadCellState {
    std::optional<float> force_g = std::nullopt; ///< Force in grams
};

inline constexpr EnumName<LoadCellRole> kLoadCellRoles[] = {
    {LoadCellRole::NONE, "none", "Unassigned"},
    {LoadCellRole::SPOOL_WEIGHT, "spool_weight", "Spool Weight"},
};

[[nodiscard]] inline std::string load_cell_role_to_string(LoadCellRole role) {
    return enum_id(kLoadCellRoles, role);
}

} // namespace helix::sensors
