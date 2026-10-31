// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afc_defaults.h"

#include "ams_device_action_rows.h"
#include "ams_device_sections.h"

#include <algorithm>

namespace helix::printer {

std::vector<DeviceSection> afc_default_sections() {
    return {
        device_section("setup", 0, TR_NOOP("Calibration, LED, and system configuration")),
        device_section("speed", 1, TR_NOOP("Move speed multipliers")),
        device_section("toolhead", 2, TR_NOOP("Extruder distances and sensor configuration")),
        device_section("maintenance", 3, TR_NOOP("Lane tests and motor resets")),
        device_section("hub", 4, TR_NOOP("Blade change and parking")),
        device_section("tip_forming", 5, TR_NOOP("Tip shaping configuration")),
        device_section("purge", 6, TR_NOOP("Purge tower and brush settings")),
        device_section("print_prefs", 7, TR_NOOP("What happens when a print ends")),
    };
}

namespace {

using V = DeviceActionRow::Value;

// clang-format off
constexpr DeviceActionRow kAfcActions[] = {
    // Setup section (calibration + LED)
    {.id = "calibration_wizard", .label = TR_NOOP("Run Calibration Wizard"), .section = "setup", .type = ActionType::BUTTON, .moves_machine = true, .icon = "play", .description = TR_NOOP("Interactive calibration for all lanes")},
    {.id = "bowden_length", .label = TR_NOOP("Bowden Length"), .section = "setup", .type = ActionType::SLIDER, .value = V::Float, .number = 450.0, .min_value = 100.0f, .max_value = 2000.0f, .unit = "mm", .icon = "ruler", .description = TR_NOOP("Distance from hub to toolhead")},

    // Speed section
    {.id = "speed_fwd", .label = TR_NOOP("Forward Multiplier"), .section = "speed", .type = ActionType::SLIDER, .value = V::Float, .number = 1.0, .min_value = 0.5f, .max_value = 2.0f, .step = 0.05f, .unit = "x", .icon = "speed_up", .description = TR_NOOP("Speed multiplier for forward moves")},
    {.id = "speed_rev", .label = TR_NOOP("Reverse Multiplier"), .section = "speed", .type = ActionType::SLIDER, .value = V::Float, .number = 1.0, .min_value = 0.5f, .max_value = 2.0f, .step = 0.05f, .unit = "x", .icon = "speed_down", .description = TR_NOOP("Speed multiplier for reverse moves")},

    // Toolhead section (extruder distances)
    {.id = "tool_stn", .label = TR_NOOP("Sensor to Nozzle"), .section = "toolhead", .type = ActionType::SLIDER, .value = V::Float, .number = 72.0, .min_value = 0.0f, .max_value = 200.0f, .unit = "mm", .icon = "ruler", .description = TR_NOOP("Distance from toolhead sensor to nozzle tip")},
    {.id = "tool_stn_unload", .label = TR_NOOP("Unload Distance"), .section = "toolhead", .type = ActionType::SLIDER, .value = V::Float, .number = 100.0, .min_value = 0.0f, .max_value = 200.0f, .unit = "mm", .icon = "ruler", .description = TR_NOOP("Retraction distance to clear extruder gears")},
    {.id = "tool_sensor_after_extruder", .label = TR_NOOP("Post-Sensor Clear"), .section = "toolhead", .type = ActionType::SLIDER, .value = V::Float, .number = 0.0, .min_value = 0.0f, .max_value = 100.0f, .unit = "mm", .icon = "ruler", .description = TR_NOOP("Extra distance to move once sensors clear")},

    // Maintenance section
    {.id = "test_lanes", .label = TR_NOOP("Test All Lanes"), .section = "maintenance", .type = ActionType::BUTTON, .moves_machine = true, .icon = "play_circle", .description = TR_NOOP("Run test sequence on all lanes")},
    {.id = "change_blade", .label = TR_NOOP("Change Blade"), .section = "maintenance", .type = ActionType::BUTTON, .moves_machine = true, .icon = "wrench", .description = TR_NOOP("Initiate blade change procedure")},
    {.id = "park", .label = TR_NOOP("Park"), .section = "maintenance", .type = ActionType::BUTTON, .moves_machine = true, .icon = "home_import", .description = TR_NOOP("Park the AFC system")},
    {.id = "brush", .label = TR_NOOP("Clean Brush"), .section = "maintenance", .type = ActionType::BUTTON, .moves_machine = true, .icon = "sync", .description = TR_NOOP("Run brush cleaning sequence")},
    {.id = "reset_motor", .label = TR_NOOP("Reset Motor Timer"), .section = "maintenance", .type = ActionType::BUTTON, .icon = "progress_clock", .description = TR_NOOP("Reset motor run-time counter")},

    // Setup section (LED & Modes)
    {.id = "led_toggle", .label = TR_NOOP("Turn On LEDs"), .section = "setup", .type = ActionType::BUTTON, .icon = "lightbulb_on", .description = TR_NOOP("Toggle AFC LED strip")},
    {.id = "led_extruder", .label = TR_NOOP("Toolhead LED"), .section = "setup", .type = ActionType::BUTTON, .icon = "lightbulb_on", .description = TR_NOOP("Toggle toolhead LED for an extruder")},
    {.id = "quiet_mode", .label = TR_NOOP("Toggle Quiet Mode"), .section = "setup", .type = ActionType::BUTTON, .icon = "volume_off", .description = TR_NOOP("Enable/disable quiet operation mode")},

    // Hub & Cutter section
    {.id = "hub_cut_enabled", .label = TR_NOOP("Cutter Enabled"), .section = "hub", .type = ActionType::TOGGLE, .value = V::Bool, .number = 0, .icon = "tune", .description = TR_NOOP("Enable or disable the hub cutter")},
    {.id = "hub_cut_dist", .label = TR_NOOP("Cut Distance"), .section = "hub", .type = ActionType::SLIDER, .value = V::Float, .number = 50.0, .min_value = 0.0f, .max_value = 100.0f, .unit = "mm", .icon = "ruler", .description = TR_NOOP("Distance for hub cutter operation")},
    {.id = "hub_bowden_length", .label = TR_NOOP("Hub Bowden Length"), .section = "hub", .type = ActionType::SLIDER, .value = V::Float, .number = 450.0, .min_value = 100.0f, .max_value = 2000.0f, .unit = "mm", .icon = "ruler", .description = TR_NOOP("Bowden tube length from hub to toolhead")},
    {.id = "assisted_retract", .label = TR_NOOP("Assisted Retract"), .section = "hub", .type = ActionType::TOGGLE, .value = V::Bool, .number = 0, .icon = "undo", .description = TR_NOOP("Enable assisted retraction at hub")},

    // Tip Forming section
    {.id = "ramming_volume", .label = TR_NOOP("Ramming Volume"), .section = "tip_forming", .type = ActionType::SLIDER, .value = V::Float, .number = 0.0, .min_value = 0.0f, .max_value = 100.0f, .unit = "mm³", .icon = "water", .description = TR_NOOP("Volume of filament used during ramming")},
    {.id = "unloading_speed_start", .label = TR_NOOP("Unloading Start Speed"), .section = "tip_forming", .type = ActionType::SLIDER, .value = V::Float, .number = 80.0, .min_value = 0.0f, .max_value = 200.0f, .unit = "mm/s", .icon = "speed", .description = TR_NOOP("Initial speed for filament unloading")},
    {.id = "cooling_tube_length", .label = TR_NOOP("Cooling Tube Length"), .section = "tip_forming", .type = ActionType::SLIDER, .value = V::Float, .number = 15.0, .min_value = 0.0f, .max_value = 100.0f, .unit = "mm", .icon = "thermometer_minus", .description = TR_NOOP("Length of the cooling tube section")},
    {.id = "cooling_tube_retraction", .label = TR_NOOP("Cooling Tube Retraction"), .section = "tip_forming", .type = ActionType::SLIDER, .value = V::Float, .number = 0.0, .min_value = 0.0f, .max_value = 100.0f, .unit = "mm", .icon = "thermometer_minus", .description = TR_NOOP("Retraction distance in the cooling tube")},

    // Purge & Wipe section
    {.id = "purge_enabled", .label = TR_NOOP("Enable Purge"), .section = "purge", .type = ActionType::TOGGLE, .value = V::Bool, .number = 0, .icon = "water", .description = TR_NOOP("Enable purging during tool changes")},
    {.id = "purge_length", .label = TR_NOOP("Purge Length"), .section = "purge", .type = ActionType::SLIDER, .value = V::Float, .number = 50.0, .min_value = 0.0f, .max_value = 200.0f, .unit = "mm", .icon = "ruler", .description = TR_NOOP("Length of filament to purge")},
    {.id = "brush_enabled", .label = TR_NOOP("Enable Brush Wipe"), .section = "purge", .type = ActionType::TOGGLE, .value = V::Bool, .number = 0, .icon = "sync", .description = TR_NOOP("Enable brush wipe after purging")},

    // Print Behavior section
    {.id = "unload_at_end", .label = TR_NOOP("Unload at end"), .section = "print_prefs", .type = ActionType::TOGGLE, .value = V::Bool, .number = 0, .icon = "filament", .description = TR_NOOP("Unload the toolhead when a print ends")},
};
// clang-format on

} // namespace

std::vector<DeviceAction> afc_default_actions() {
    return make_device_actions(kAfcActions);
}

void afc_add_toolhead_calibration_action(std::vector<DeviceAction>& actions) {
    const auto first_toolhead = std::find_if(actions.begin(), actions.end(),
                                             [](const auto& a) { return a.section == "toolhead"; });
    actions.insert(first_toolhead,
                   DeviceAction::button("toolhead_calibration", "Calibrate Toolhead", "toolhead",
                                        "play",
                                        "Guided sensor-to-nozzle, unload and cutter retract "
                                        "calibration")
                       .with_machine_motion());
}

AfcCapabilities afc_default_capabilities() {
    return {
        .supports_endless_spool = true,
        .supports_bypass = true,
        .supports_purge = true,
        .tip_method = TipMethod::CUT,
    };
}

} // namespace helix::printer
