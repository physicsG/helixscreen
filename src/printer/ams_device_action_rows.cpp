// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_device_action_rows.h"

namespace helix::printer {

namespace {

std::any row_value(const DeviceActionRow& row) {
    using V = DeviceActionRow::Value;
    switch (row.value) {
    case V::Bool:
        return row.number != 0;
    case V::Float:
        return static_cast<float>(row.number);
    case V::Double:
        return row.number;
    case V::String:
        return std::string(row.text ? row.text : "");
    case V::None:
        break;
    }
    return {};
}

} // namespace

DeviceAction make_device_action(const DeviceActionRow& row) {
    DeviceAction a;
    a.id = row.id;
    a.label = row.label;
    a.section = row.section;
    if (row.icon) {
        a.icon = row.icon;
    }
    if (row.description) {
        a.description = row.description;
    }
    a.type = row.type;
    a.current_value = row_value(row);
    a.options.assign(row.options, row.options + row.option_count);
    a.min_value = row.min_value;
    a.max_value = row.max_value;
    a.step = row.step;
    if (row.unit) {
        a.unit = row.unit;
    }
    a.moves_machine = row.moves_machine;
    a.allowed_while_paused = row.allowed_while_paused;
    return a;
}

std::vector<DeviceAction> make_device_actions(const DeviceActionRow* rows, size_t count) {
    std::vector<DeviceAction> actions;
    actions.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        actions.push_back(make_device_action(rows[i]));
    }
    return actions;
}

} // namespace helix::printer
