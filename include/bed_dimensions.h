// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class IMoonrakerAPI;

namespace helix {

class PrinterState;

struct BedDimensions {
    float w_mm;
    float h_mm;
    float origin_x;
    float origin_y;
};

/// Fallback used when no source reports a usable bed size.
inline constexpr float kDefaultBedSizeMm = 235.0f;

/// Pure form: derive dimensions from an axis range, falling back to the default
/// when the range is degenerate (zero or negative extent).
BedDimensions bed_dimensions_from_volume(float x_min, float x_max, float y_min, float y_max);

/// Resolve the bed size from the best available source, in order:
///   1. IMoonrakerAPI::hardware().build_volume() — from Klipper
///      configfile.settings.stepper_x/y position_min/position_max
///   2. PrinterState::get_axis_bounds() — toolhead.axis_minimum/axis_maximum,
///      the kinematic envelope, available earlier than (1) because it rides the
///      status subscription rather than a config query
///   3. 235x235 with a zero origin
///
/// Either argument may be null. Do NOT add printer_database.json's
/// `build_volume_range` as a source — that field is a detection heuristic, not
/// an authoritative bed size.
BedDimensions bed_dimensions(IMoonrakerAPI* api, const PrinterState* ps);

} // namespace helix
