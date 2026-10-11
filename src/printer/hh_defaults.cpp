// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hh_defaults.h"

namespace helix::printer {

std::vector<DeviceSection> hh_default_sections() {
    return {
        {"setup", "Setup", 0, "Calibration and system configuration"},
        {"speed", "Speed", 1, "Motor speeds and acceleration"},
        {"toolhead", "Toolhead", 2, "Extruder distances and sensor configuration"},
        {"accessories", "Accessories", 3, "eSpooler, clog detection, and sensor settings"},
        {"maintenance", "Maintenance", 4, "Testing, servo, and motor operations"},
    };
}

std::vector<DeviceAction> hh_default_actions() {
    using DA = DeviceAction;
    using Str = std::string;
    return {
        // --- Setup section ---
        DA::button("calibrate_bowden", "Calibrate Bowden", "setup"),
        DA::button("calibrate_encoder", "Calibrate Encoder", "setup"),
        DA::button("calibrate_gear", "Calibrate Gear", "setup"),
        DA::button("calibrate_gates", "Calibrate Gates", "setup"),
        DA::dropdown("led_mode", "LED Mode", "setup",
                     {"off", "gate_status", "filament_color", "on"}, Str("off")),

        // --- Speed section ---
        DA::slider("gear_from_buffer_speed", "Gear Buffer Speed", "speed", 150.0, 10.0f, 300.0f,
                   "mm/s"),
        DA::slider("gear_from_spool_speed", "Gear Spool Speed", "speed", 60.0, 10.0f, 300.0f,
                   "mm/s"),
        DA::slider("gear_unload_speed", "Gear Unload Speed", "speed", 80.0, 10.0f, 300.0f, "mm/s"),
        DA::slider("selector_speed", "Selector Speed", "speed", 200.0, 10.0f, 300.0f, "mm/s"),
        DA::slider("extruder_load_speed", "Extruder Load Speed", "speed", 45.0, 10.0f, 100.0f,
                   "mm/s"),
        DA::slider("extruder_unload_speed", "Extruder Unload Speed", "speed", 45.0, 10.0f, 100.0f,
                   "mm/s"),

        // --- Toolhead section ---
        DA::slider("toolhead_sensor_to_nozzle", "Sensor to Nozzle", "toolhead", 62.0, 1.0f, 200.0f,
                   "mm"),
        DA::slider("toolhead_extruder_to_nozzle", "Extruder to Nozzle", "toolhead", 72.0, 5.0f,
                   200.0f, "mm"),
        DA::slider("toolhead_entry_to_extruder", "Entry to Extruder", "toolhead", 0.0, 0.0f, 200.0f,
                   "mm"),
        DA::slider("toolhead_ooze_reduction", "Ooze Reduction", "toolhead", 2.0, -5.0f, 20.0f,
                   "mm"),

        // --- Accessories section (v4) ---
        DA::dropdown("espooler_mode", "eSpooler Mode", "accessories", {"off", "rewind", "assist"},
                     Str("off")),
        DA::dropdown("clog_detection", "Clog Detection", "accessories", {"Off", "Manual", "Auto"},
                     Str("Off")),
        DA::toggle("sync_to_extruder", "Sync during printing", "accessories", false),
        DA::button("spoolman_refresh", "Refresh Spoolman", "accessories"),

        // --- Maintenance section ---
        DA::button("load_extruder", "Load Extruder", "maintenance"),
        DA::button("unload_extruder", "Unload Extruder", "maintenance"),
        DA::button("test_grip", "Test Grip", "maintenance"),
        DA::button("test_load", "Test Load", "maintenance"),
        DA::button("test_move", "Test Move", "maintenance"),
        DA::toggle("gear_sync", "Gear motor synced", "maintenance", false),
        DA::toggle("motors_toggle", "Motors", "maintenance", true),
        DA::button("servo_buzz", "Buzz Servo", "maintenance"),
        DA::button("servo_up", "Servo Up", "maintenance"),
        DA::button("servo_move", "Servo Move", "maintenance"),
        DA::button("servo_down", "Servo Down", "maintenance"),
        DA::button("reset_servo_counter", "Reset Servo Counter", "maintenance"),
        DA::button("reset_blade_counter", "Reset Blade Counter", "maintenance"),
    };
}

} // namespace helix::printer
