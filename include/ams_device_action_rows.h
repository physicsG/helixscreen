// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ams_types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * @file ams_device_action_rows.h
 * @brief A backend's fixed device-action list declared as constant data
 *
 * A row is plain constant data, so a table of them costs a few dozen bytes per
 * action where each DeviceAction factory call expands into several std::string
 * and std::any constructions. Wrap label and description in TR_NOOP() so the
 * translation extractor finds them.
 */
namespace helix::printer {

struct DeviceActionRow {
    /// The type stored in DeviceAction::current_value; the UI and the backend cast it back.
    enum class Value : uint8_t { None, Bool, Float, Double, String };

    const char* id;
    const char* label;
    const char* section;
    ActionType type;
    Value value = Value::None;
    double number = 0;          ///< Bool, Float and Double values; nonzero is true
    const char* text = nullptr; ///< String value
    float min_value = 0;
    float max_value = 100;
    float step = 1;
    const char* unit = nullptr;
    const char* const* options = nullptr; ///< Dropdown choices
    size_t option_count = 0;
    bool moves_machine = false;
    bool allowed_while_paused = false;
    const char* icon = nullptr;
    const char* description = nullptr;
};

[[nodiscard]] DeviceAction make_device_action(const DeviceActionRow& row);
[[nodiscard]] std::vector<DeviceAction> make_device_actions(const DeviceActionRow* rows,
                                                            size_t count);

template <size_t N>
[[nodiscard]] std::vector<DeviceAction> make_device_actions(const DeviceActionRow (&rows)[N]) {
    return make_device_actions(rows, N);
}

} // namespace helix::printer
