// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file leveling_probe_points.h
 * @brief Which gantry/bed leveling phase a probe XY belongs to
 *
 * Quad gantry leveling and Z tilt adjust probe the XY points listed in the
 * printer's `[quad_gantry_level] points` / `[z_tilt] points`, with the toolhead
 * itself at each point (no probe offset applied). Klipper prints the same
 * "probe at X,Y is z=Z" line for these as for a bed mesh and echoes nothing a
 * macro runs, so matching the probe XY against the configured points is the
 * one leveling signal every such printer emits.
 *
 * Pure: no LVGL, no threads.
 */

#include "print_start_phase.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace helix {

/// One configured leveling probe point and the phase that probes it.
struct LevelingProbePoint {
    double x = 0.0;
    double y = 0.0;
    PrintStartPhase phase = PrintStartPhase::QGL; ///< QGL or Z_TILT
};

/// Probe and toolhead XY land on the configured point to the step, so this
/// only absorbs float formatting; real mesh spacing is tens of millimetres.
inline constexpr double LEVELING_POINT_TOLERANCE_MM = 1.5;

/// Index of the configured point at (x, y), or -1 when none is within tolerance.
inline int leveling_point_index(const std::vector<LevelingProbePoint>& points, double x, double y,
                                double tolerance_mm = LEVELING_POINT_TOLERANCE_MM) {
    for (size_t i = 0; i < points.size(); ++i) {
        if (std::fabs(points[i].x - x) <= tolerance_mm &&
            std::fabs(points[i].y - y) <= tolerance_mm) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// The leveling phase that probes (x, y), or nullopt when no configured point is there.
inline std::optional<PrintStartPhase>
leveling_phase_at(const std::vector<LevelingProbePoint>& points, double x, double y) {
    const int i = leveling_point_index(points, x, y);
    if (i < 0) {
        return std::nullopt;
    }
    return points[static_cast<size_t>(i)].phase;
}

/**
 * @brief Recognises leveling from toolhead position alone
 *
 * A probe that prints no "probe at" line still moves the toolhead down at each
 * configured point: Z lower than the previous sample at the same point,
 * whatever the travel height or probe depth. A descent at one point is not enough: a park at a
 * corner that happens to be a leveling point does the same. Descents at two
 * distinct points are.
 */
class LevelingDescentTracker {
  public:
    /// Feed one toolhead sample; returns the phase once two points have seen a descent.
    std::optional<PrintStartPhase> note_position(const std::vector<LevelingProbePoint>& points,
                                                 double x, double y, double z) {
        const int i = leveling_point_index(points, x, y);
        if (i >= 0 && i < 32 && i == last_index_ && z < last_z_) {
            descended_ |= (1u << i);
        }
        last_index_ = i;
        last_z_ = z;
        if (i < 0 || (descended_ & (descended_ - 1)) == 0) {
            return std::nullopt; // fewer than two distinct points have descended
        }
        return points[static_cast<size_t>(i)].phase;
    }

    void reset() {
        last_index_ = -1;
        last_z_ = 0.0;
        descended_ = 0;
    }

  private:
    int last_index_ = -1;
    double last_z_ = 0.0;
    uint32_t descended_ = 0; ///< bit i set: point i has seen a descent
};

} // namespace helix
