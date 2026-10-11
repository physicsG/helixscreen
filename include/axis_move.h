// SPDX-License-Identifier: GPL-3.0-or-later
// include/axis_move.h
#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace helix {

/** Relative multi-axis jog delta in mm. */
struct AxisMove {
    double dx = 0.0;
    double dy = 0.0;
    double dz = 0.0;
    // Mixed-magnitude float cancellation can leave ~1e-17 residue in an
    // accumulated delta; treat anything below this as zero so a near-null move
    // (which would serialize in scientific notation) never flushes.
    static constexpr double EPSILON_MM = 1e-6;
    bool any() const {
        return std::abs(dx) > EPSILON_MM || std::abs(dy) > EPSILON_MM || std::abs(dz) > EPSILON_MM;
    }
};

/** Absolute multi-axis target in mm; an unset axis is not commanded. */
struct AxisTarget {
    std::optional<double> x;
    std::optional<double> y;
    std::optional<double> z;
    bool any() const {
        return x.has_value() || y.has_value() || z.has_value();
    }
};

/** Clamp each SET axis of an absolute target into its range; unset axes pass
 *  through untouched. The z range is optional as a whole: without it a set z
 *  passes through unclamped (the caller decides separately whether that is
 *  grounds to refuse the move). */
inline AxisTarget clamp_target_to_bounds(AxisTarget target, double x_min, double x_max,
                                         double y_min, double y_max,
                                         std::optional<std::pair<double, double>> z_range) {
    if (target.x) {
        target.x = std::clamp(*target.x, x_min, x_max);
    }
    if (target.y) {
        target.y = std::clamp(*target.y, y_min, y_max);
    }
    if (target.z && z_range) {
        target.z = std::clamp(*target.z, z_range->first, z_range->second);
    }
    return target;
}

} // namespace helix
