// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hh_defaults.h"

#include "ams_device_action_rows.h"
#include "ams_device_sections.h"

#include <iterator>

namespace helix::printer {

std::vector<DeviceSection> hh_default_sections() {
    return {
        device_section("setup", 0, TR_NOOP("Calibration and system configuration")),
        device_section("speed", 1, TR_NOOP("Motor speeds and acceleration")),
        device_section("toolhead", 2, TR_NOOP("Extruder distances and sensor configuration")),
        device_section("accessories", 3, TR_NOOP("eSpooler, clog detection, and sensor settings")),
        device_section("maintenance", 4, TR_NOOP("Testing, servo, and motor operations")),
    };
}

namespace {

using V = DeviceActionRow::Value;

constexpr const char* kLedModes[] = {"off", "gate_status", "filament_color", "on"};
constexpr const char* kEspoolerModes[] = {"off", "rewind", "assist"};
constexpr const char* kClogDetectionModes[] = {"Off", "Manual", "Auto"};

// clang-format off
constexpr DeviceActionRow kHhActions[] = {
    // --- Setup section ---
    {.id = "calibrate_bowden", .label = TR_NOOP("Calibrate Bowden"), .section = "setup", .type = ActionType::BUTTON, .moves_machine = true},
    {.id = "calibrate_encoder", .label = TR_NOOP("Calibrate Encoder"), .section = "setup", .type = ActionType::BUTTON, .moves_machine = true},
    {.id = "calibrate_gear", .label = TR_NOOP("Calibrate Gear"), .section = "setup", .type = ActionType::BUTTON, .moves_machine = true},
    {.id = "calibrate_gates", .label = TR_NOOP("Calibrate Gates"), .section = "setup", .type = ActionType::BUTTON, .moves_machine = true},
    {.id = "led_mode", .label = TR_NOOP("LED Mode"), .section = "setup", .type = ActionType::DROPDOWN, .value = V::String, .text = "off", .options = kLedModes, .option_count = std::size(kLedModes)},

    // --- Speed section ---
    {.id = "gear_from_buffer_speed", .label = TR_NOOP("Gear Buffer Speed"), .section = "speed", .type = ActionType::SLIDER, .value = V::Double, .number = 150.0, .min_value = 10.0f, .max_value = 300.0f, .unit = "mm/s"},
    {.id = "gear_from_spool_speed", .label = TR_NOOP("Gear Spool Speed"), .section = "speed", .type = ActionType::SLIDER, .value = V::Double, .number = 60.0, .min_value = 10.0f, .max_value = 300.0f, .unit = "mm/s"},
    {.id = "gear_unload_speed", .label = TR_NOOP("Gear Unload Speed"), .section = "speed", .type = ActionType::SLIDER, .value = V::Double, .number = 80.0, .min_value = 10.0f, .max_value = 300.0f, .unit = "mm/s"},
    {.id = "selector_speed", .label = TR_NOOP("Selector Speed"), .section = "speed", .type = ActionType::SLIDER, .value = V::Double, .number = 200.0, .min_value = 10.0f, .max_value = 300.0f, .unit = "mm/s"},
    {.id = "extruder_load_speed", .label = TR_NOOP("Extruder Load Speed"), .section = "speed", .type = ActionType::SLIDER, .value = V::Double, .number = 45.0, .min_value = 10.0f, .max_value = 100.0f, .unit = "mm/s"},
    {.id = "extruder_unload_speed", .label = TR_NOOP("Extruder Unload Speed"), .section = "speed", .type = ActionType::SLIDER, .value = V::Double, .number = 45.0, .min_value = 10.0f, .max_value = 100.0f, .unit = "mm/s"},

    // --- Toolhead section ---
    {.id = "toolhead_sensor_to_nozzle", .label = TR_NOOP("Sensor to Nozzle"), .section = "toolhead", .type = ActionType::SLIDER, .value = V::Double, .number = 62.0, .min_value = 1.0f, .max_value = 200.0f, .step = 0.1f, .unit = "mm"},
    {.id = "toolhead_extruder_to_nozzle", .label = TR_NOOP("Extruder to Nozzle"), .section = "toolhead", .type = ActionType::SLIDER, .value = V::Double, .number = 72.0, .min_value = 5.0f, .max_value = 200.0f, .step = 0.1f, .unit = "mm"},
    {.id = "toolhead_entry_to_extruder", .label = TR_NOOP("Entry to Extruder"), .section = "toolhead", .type = ActionType::SLIDER, .value = V::Double, .number = 0.0, .min_value = 0.0f, .max_value = 200.0f, .step = 0.1f, .unit = "mm"},
    {.id = "toolhead_ooze_reduction", .label = TR_NOOP("Ooze Reduction"), .section = "toolhead", .type = ActionType::SLIDER, .value = V::Double, .number = 2.0, .min_value = -5.0f, .max_value = 20.0f, .step = 0.1f, .unit = "mm"},

    // --- Accessories section (v4) ---
    {.id = "espooler_mode", .label = TR_NOOP("eSpooler Mode"), .section = "accessories", .type = ActionType::DROPDOWN, .value = V::String, .text = "off", .options = kEspoolerModes, .option_count = std::size(kEspoolerModes)},
    {.id = "clog_detection", .label = TR_NOOP("Clog Detection"), .section = "accessories", .type = ActionType::DROPDOWN, .value = V::String, .text = "Off", .options = kClogDetectionModes, .option_count = std::size(kClogDetectionModes)},
    {.id = "sync_to_extruder", .label = TR_NOOP("Sync during printing"), .section = "accessories", .type = ActionType::TOGGLE, .value = V::Bool, .number = 0},
    {.id = "spoolman_refresh", .label = TR_NOOP("Refresh Spoolman"), .section = "accessories", .type = ActionType::BUTTON},

    // --- Maintenance section ---
    {.id = "load_extruder", .label = TR_NOOP("Load Extruder"), .section = "maintenance", .type = ActionType::BUTTON, .moves_machine = true, .allowed_while_paused = true},
    {.id = "unload_extruder", .label = TR_NOOP("Unload Extruder"), .section = "maintenance", .type = ActionType::BUTTON, .moves_machine = true, .allowed_while_paused = true},
    {.id = "test_grip", .label = TR_NOOP("Test Grip"), .section = "maintenance", .type = ActionType::BUTTON},
    {.id = "test_load", .label = TR_NOOP("Test Load"), .section = "maintenance", .type = ActionType::BUTTON, .moves_machine = true},
    {.id = "test_move", .label = TR_NOOP("Test Move"), .section = "maintenance", .type = ActionType::BUTTON, .moves_machine = true},
    {.id = "gear_sync", .label = TR_NOOP("Gear motor synced"), .section = "maintenance", .type = ActionType::TOGGLE, .value = V::Bool, .number = 0},
    {.id = "motors_toggle", .label = TR_NOOP("Motors"), .section = "maintenance", .type = ActionType::TOGGLE, .value = V::Bool, .number = 1},
    {.id = "servo_buzz", .label = TR_NOOP("Buzz Servo"), .section = "maintenance", .type = ActionType::BUTTON},
    {.id = "servo_up", .label = TR_NOOP("Servo Up"), .section = "maintenance", .type = ActionType::BUTTON},
    {.id = "servo_move", .label = TR_NOOP("Servo Move"), .section = "maintenance", .type = ActionType::BUTTON},
    {.id = "servo_down", .label = TR_NOOP("Servo Down"), .section = "maintenance", .type = ActionType::BUTTON},
    {.id = "reset_servo_counter", .label = TR_NOOP("Reset Servo Counter"), .section = "maintenance", .type = ActionType::BUTTON},
    {.id = "reset_blade_counter", .label = TR_NOOP("Reset Blade Counter"), .section = "maintenance", .type = ActionType::BUTTON},
};
// clang-format on

} // namespace

std::vector<DeviceAction> hh_default_actions() {
    return make_device_actions(kHhActions);
}

std::pair<float, float> hh_default_action_range(std::string_view id) {
    for (const auto& row : kHhActions) {
        if (id == row.id) {
            return {row.min_value, row.max_value};
        }
    }
    return {-1e6f, 1e6f};
}

} // namespace helix::printer
