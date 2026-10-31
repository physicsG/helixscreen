// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <utility>
#include <vector>

namespace helix {

/// Spacing of the reference grid every bed view draws, in printer mm.
inline constexpr double kBedGridSpacingMm = 50.0;

/// One grid line in printer mm.
struct BedGridSegment {
    double x1, y1, x2, y2;
};

/**
 * @brief Interior grid lines of a bed, at whole multiples of @p spacing_mm
 *
 * A rectangular bed gets a line at every multiple strictly inside
 * [x_min, x_max] x [y_min, y_max], each spanning the box edge to edge. A round
 * bed is the circle inscribed in that box (radius = the smaller half-size), and
 * each line is its chord. Lines on the perimeter are left out on both shapes:
 * every caller draws its own outline.
 *
 * Empty for a degenerate box or a non-positive spacing.
 */
std::vector<BedGridSegment> bed_grid_lines(double x_min, double x_max, double y_min, double y_max,
                                           double spacing_mm, bool circular);

/**
 * @brief A bed's outline: its four edges, or the kRoundBedSegments-gon on the
 *        circle inscribed in the box for a round bed
 *
 * Empty for a degenerate box.
 */
std::vector<BedGridSegment> bed_outline_segments(double x_min, double x_max, double y_min,
                                                 double y_max, bool circular);

/// A point in printer mm.
struct BedPoint {
    double x, y;
};

/// A round bed: the circle inscribed in its bounding box.
struct RoundBed {
    double cx, cy, r;
};

/// The circle inscribed in [x_min, x_max] x [y_min, y_max].
RoundBed inscribed_round_bed(double x_min, double x_max, double y_min, double y_max);

/// Straight segments every view uses to approximate a round bed's outline.
inline constexpr int kRoundBedSegments = 72;

/**
 * @brief Clip a convex polygon to a round bed
 *
 * The circle is the regular kRoundBedSegments-gon with its vertices on it.
 * A polygon wholly inside comes back unchanged; one wholly outside comes back
 * empty.
 */
std::vector<BedPoint> clip_to_round_bed(const std::vector<BedPoint>& polygon, const RoundBed& bed);

/**
 * @brief The part of segment a->b inside a round bed, as parameters along it
 *
 * {t0, t1} with 0 <= t0 < t1 <= 1, or nullopt when the segment stays outside.
 */
std::optional<std::pair<double, double>> segment_in_round_bed(BedPoint a, BedPoint b,
                                                              const RoundBed& bed);

} // namespace helix
