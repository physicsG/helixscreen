// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

/**
 * @file filament_widget_tap_policy.h
 * @brief Where a tap on the Filament Sensor home tile goes, and which sensor it follows.
 *
 * Taken as plain ints rather than reading PrinterState so the whole decision is
 * testable in a binary with no printer and no display - the same seam
 * filament_op_dispatch.h uses.
 */

namespace helix::ui {

/// Where a tap on the Filament Sensor tile lands.
enum class FilamentTapDestination {
    None,            ///< Tap is not actionable (no sensor configured)
    SensorSettings,  ///< Sensor is disabled; open SensorSettingsOverlay
    ModalStatusOnly, ///< Printing; show the modal with no manual actions
    ModalFull,       ///< Idle/paused/error; Load, Unload and Purge available
};

/**
 * @brief Decide where a tap goes.
 *
 * @param sensor_state -1 none / 0 empty / 1 loaded / 2 configured-but-disabled,
 *                     as encoded by FilamentSensorManager.
 * @param print_state  0 standby / 1 printing / 2 paused / 5 error.
 *
 * Order is deliberate. A disabled sensor is a configuration problem in every
 * print state and the load/unload dialog cannot fix it, so that check precedes
 * the printing check. Load/Unload/Purge mid-print destroys the print, so a
 * running print reduces the modal to a status readout rather than disabling the
 * tile outright - the tile stays informative exactly when it is being watched.
 */
[[nodiscard]] inline FilamentTapDestination decide_tap_destination(int sensor_state,
                                                                   int print_state) {
    if (sensor_state < 0) {
        return FilamentTapDestination::None;
    }
    if (sensor_state == 2) {
        return FilamentTapDestination::SensorSettings;
    }
    if (print_state == 1) {
        return FilamentTapDestination::ModalStatusOnly;
    }
    return FilamentTapDestination::ModalFull;
}

/// Which sensor role the tile mirrors. Auto follows the runout-role sensor,
/// which is what the tile did before it was configurable.
enum class FilamentTileSource {
    Auto,
    Runout,
    Toolhead,
    Entry,
};

/// Parse the saved widget config value. Unknown, empty and wrong-case values
/// fall back to Auto rather than blanking the tile.
[[nodiscard]] inline FilamentTileSource parse_tile_source(const std::string& value) {
    if (value == "runout") {
        return FilamentTileSource::Runout;
    }
    if (value == "toolhead") {
        return FilamentTileSource::Toolhead;
    }
    if (value == "entry") {
        return FilamentTileSource::Entry;
    }
    return FilamentTileSource::Auto;
}

/// Inverse of parse_tile_source(), for persisting to widget config.
[[nodiscard]] inline const char* tile_source_to_string(FilamentTileSource source) {
    switch (source) {
    case FilamentTileSource::Runout:
        return "runout";
    case FilamentTileSource::Toolhead:
        return "toolhead";
    case FilamentTileSource::Entry:
        return "entry";
    case FilamentTileSource::Auto:
        break;
    }
    return "auto";
}

/// The FilamentSensorManager subject the tile's mirror follows for a source.
/// Auto and Runout coincide today; they are separate enum values so that
/// "the user pinned Runout" survives a future change to what Auto means.
[[nodiscard]] inline const char* tile_source_subject(FilamentTileSource source) {
    switch (source) {
    case FilamentTileSource::Toolhead:
        return "filament_toolhead_detected";
    case FilamentTileSource::Entry:
        return "filament_entry_detected";
    case FilamentTileSource::Runout:
    case FilamentTileSource::Auto:
        break;
    }
    return "filament_runout_detected";
}

} // namespace helix::ui
