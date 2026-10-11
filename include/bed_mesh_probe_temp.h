// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace helix {
namespace bed_mesh {

/// Bed temperature a mesh is probed at when nothing else chooses one.
inline constexpr int DEFAULT_PROBE_BED_TEMP_C = 60;

/**
 * @brief The bed temperature, in whole degrees C, to probe a mesh at.
 *
 * Firmware heat-and-wait commands can wait for a bed to cool as well as to heat,
 * so a bed above the default is never asked for less than it already has:
 * - the temperature wanted is a set target, or the default;
 * - a bed above the default that is hotter than that is probed where it is,
 *   floored so the wait is satisfied at once.
 *
 * @param target_deci  Bed target in decidegrees, as PrinterState holds it.
 * @param current_deci Bed temperature in decidegrees.
 */
[[nodiscard]] inline constexpr int probe_bed_temp_c(int target_deci, int current_deci) {
    const int target_c = target_deci / 10;
    const int wanted_c = target_c > 0 ? target_c : DEFAULT_PROBE_BED_TEMP_C;
    if (current_deci <= DEFAULT_PROBE_BED_TEMP_C * 10) {
        return wanted_c;
    }
    const int current_c = current_deci / 10;
    return current_c > wanted_c ? current_c : wanted_c;
}

} // namespace bed_mesh
} // namespace helix
