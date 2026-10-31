// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ams_types.h"
#include "translation_loader.h" // TR_NOOP

#include <string>
#include <string_view>

/**
 * @file ams_device_sections.h
 * @brief The one vocabulary of AMS device-settings sections
 *
 * Every backend groups its DeviceActions under these ids, so the same kind of
 * setting has the same name and icon on every filament system. A backend picks
 * the ids it uses, their order and a description of what its own section holds;
 * the label and icon come from here. Labels are English: the UI translates them.
 */

namespace helix::printer {

struct DeviceSectionKind {
    const char* id;
    const char* label;
    const char* icon;
};

/// Every section a backend may declare.
inline constexpr DeviceSectionKind kDeviceSectionKinds[] = {
    {"setup", TR_NOOP("Setup"), "cog"},
    {"calibration", TR_NOOP("Calibration"), "target"},
    {"speed", TR_NOOP("Speed"), "speed_up"},
    {"toolhead", TR_NOOP("Toolhead"), "nozzle"},
    {"filament_control", TR_NOOP("Filament Control"), "swap_vertical"},
    {"feeder", TR_NOOP("Filament Feeder"), "lock_open"},
    {"accessories", TR_NOOP("Accessories"), "puzzle_outline"},
    {"endless_spool", TR_NOOP("Endless Spool"), "infinity"},
    {"print_prefs", TR_NOOP("Print Behavior"), "tune"},
    {"hub", TR_NOOP("Hub & Cutter"), "source_branch"},
    {"tip_forming", TR_NOOP("Tip Forming"), "thermometer"},
    {"purge", TR_NOOP("Purge & Wipe"), "water"},
    {"maintenance", TR_NOOP("Maintenance"), "wrench"},
    {"tool_offsets", TR_NOOP("Tool Offsets"), "axis_arrow"},
};

/// The kind named @p id, or nullptr for an id outside the vocabulary.
[[nodiscard]] inline const DeviceSectionKind* find_device_section_kind(std::string_view id) {
    for (const auto& kind : kDeviceSectionKinds) {
        if (id == kind.id) {
            return &kind;
        }
    }
    return nullptr;
}

/**
 * @brief The section @p id with the shared label and icon.
 *
 * An id outside kDeviceSectionKinds is a programming error the section-vocabulary
 * test catches; it still renders, labeled with its id and the cog icon.
 *
 * @param description What this backend's section holds, in English (TR_NOOP it).
 */
[[nodiscard]] inline DeviceSection device_section(std::string_view id, int display_order,
                                                  std::string description) {
    const DeviceSectionKind* kind = find_device_section_kind(id);
    DeviceSection section;
    section.id = std::string(id);
    section.label = kind ? kind->label : std::string(id);
    section.display_order = display_order;
    section.description = std::move(description);
    section.icon = kind ? kind->icon : "cog";
    return section;
}

} // namespace helix::printer
