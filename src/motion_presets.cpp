// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "motion_presets.h"

#include <algorithm>
#include <cmath>

namespace helix {

namespace {

/// Rectangular beds: an edge preset sits this fraction of the axis's span
/// in from the edge, i.e. half-span minus 10% leaves 40% of reach.
constexpr double PRESET_INSET_FRACTION = 0.1;

/// Circular beds: rim presets sit at this fraction of the inscribed radius.
constexpr double PRESET_RADIUS_FRACTION = 0.9;

/// Midpoint of one axis's kinematic range. A range the printer has not sent
/// yet has no midpoint, and a caller that guesses one aims machine motion at
/// fabricated coordinates.
std::optional<float> axis_center(bool known, float lo, float hi) {
    if (!known || lo >= hi) {
        return std::nullopt;
    }
    return (lo + hi) / 2.0f;
}

/// Radius of the circle inscribed in an area: the reach of a round bed whose
/// bounds are that area.
double inscribed_radius(const AxisBounds& area) {
    return std::min(area.x_max - area.x_min, area.y_max - area.y_min) / 2.0;
}

/// Where a preset sits on the grid: -1 min side, 0 centre, 1 max side.
struct GridPosition {
    int x;
    int y;
};

constexpr GridPosition grid_position(MotionPreset preset) {
    switch (preset) {
    case MotionPreset::RearLeft:
        return {-1, 1};
    case MotionPreset::Rear:
        return {0, 1};
    case MotionPreset::RearRight:
        return {1, 1};
    case MotionPreset::Left:
        return {-1, 0};
    case MotionPreset::Center:
        return {0, 0};
    case MotionPreset::Right:
        return {1, 0};
    case MotionPreset::FrontLeft:
        return {-1, -1};
    case MotionPreset::Front:
        return {0, -1};
    case MotionPreset::FrontRight:
        return {1, -1};
    }
    return {0, 0};
}

} // namespace

AxisBounds preset_area(const AxisBounds& machine_travel, const AxisBounds& gcode_travel,
                       const BuildVolume& volume) {
    AxisBounds area = gcode_travel;
    if (volume.plate_x_max <= volume.plate_x_min || volume.plate_y_max <= volume.plate_y_min) {
        return area;
    }
    // machine = gcode + origin, and travel carries both forms of each limit.
    const float origin_x = machine_travel.x_min - gcode_travel.x_min;
    const float origin_y = machine_travel.y_min - gcode_travel.y_min;
    area.x_min = std::max(gcode_travel.x_min, volume.plate_x_min - origin_x);
    area.x_max = std::min(gcode_travel.x_max, volume.plate_x_max - origin_x);
    area.y_min = std::max(gcode_travel.y_min, volume.plate_y_min - origin_y);
    area.y_max = std::min(gcode_travel.y_max, volume.plate_y_max - origin_y);
    return area;
}

std::optional<AxisTarget> motion_preset_target(MotionPreset preset, const AxisBounds& gcode_bounds,
                                               bool circular_bed) {
    const auto center_x = axis_center(gcode_bounds.has_x, gcode_bounds.x_min, gcode_bounds.x_max);
    const auto center_y = axis_center(gcode_bounds.has_y, gcode_bounds.y_min, gcode_bounds.y_max);
    if (!center_x || !center_y) {
        return std::nullopt;
    }

    const auto [col, row] = grid_position(preset);
    AxisTarget target;

    if (col == 0 && row == 0) {
        target.x = *center_x;
        target.y = *center_y;
        return target;
    }

    if (circular_bed) {
        // Inscribed circle of the bounding square: 90% of the radius in the
        // preset's direction, so diagonals land at 45 degrees, not on the
        // square's corners where the round bed ends.
        const double radius = inscribed_radius(gcode_bounds);
        const double diagonal = std::sqrt(static_cast<double>(col) * col + row * row);
        const double reach = PRESET_RADIUS_FRACTION * radius / diagonal;
        target.x = *center_x + col * reach;
        target.y = *center_y + row * reach;
    } else {
        const double reach_x =
            (gcode_bounds.x_max - gcode_bounds.x_min) * (0.5 - PRESET_INSET_FRACTION);
        const double reach_y =
            (gcode_bounds.y_max - gcode_bounds.y_min) * (0.5 - PRESET_INSET_FRACTION);
        target.x = *center_x + col * reach_x;
        target.y = *center_y + row * reach_y;
    }
    return target;
}

std::optional<AxisTarget> plate_rear_park(const AxisBounds& area) {
    const auto center_x = axis_center(area.has_x, area.x_min, area.x_max);
    const auto center_y = axis_center(area.has_y, area.y_min, area.y_max);
    if (!center_x || !center_y) {
        return std::nullopt;
    }
    AxisTarget target;
    target.x = *center_x;
    target.y = std::max(static_cast<double>(*center_y),
                        static_cast<double>(area.y_max) - PARK_REAR_MARGIN_MM);
    return target;
}

BedCoordMapper bed_map_mapper(const AxisBounds& area, int viewport_w_px, int viewport_h_px) {
    return BedCoordMapper(area.x_max - area.x_min, area.y_max - area.y_min, viewport_w_px,
                          viewport_h_px, area.x_min, area.y_min);
}

std::optional<AxisTarget> bed_map_target(float x_px, float y_px, const BedCoordMapper& mapper,
                                         const AxisBounds& area, bool circular_bed) {
    const auto center_x = axis_center(area.has_x, area.x_min, area.x_max);
    const auto center_y = axis_center(area.has_y, area.y_min, area.y_max);
    if (!center_x || !center_y) {
        return std::nullopt;
    }
    const auto [mm_x, mm_y] = mapper.px_to_mm(x_px, y_px);
    double x = mm_x;
    double y = mm_y;
    if (circular_bed) {
        const double radius = inscribed_radius(area);
        const double dx = x - *center_x;
        const double dy = y - *center_y;
        const double dist = std::hypot(dx, dy);
        if (dist > radius) {
            x = *center_x + dx * radius / dist;
            y = *center_y + dy * radius / dist;
        }
    }
    AxisTarget target;
    target.x = std::clamp(x, static_cast<double>(area.x_min), static_cast<double>(area.x_max));
    target.y = std::clamp(y, static_cast<double>(area.y_min), static_cast<double>(area.y_max));
    return target;
}

float bed_map_guide_half_span(float offset, float half_extent, bool circular) {
    if (!circular) {
        return half_extent;
    }
    const float squared = half_extent * half_extent - offset * offset;
    return squared > 0.0f ? std::sqrt(squared) : 0.0f;
}

std::optional<double> bed_map_lift_z(double current_z, double clearance_mm, double z_max) {
    const double lift = std::min(clearance_mm, z_max);
    if (current_z >= lift - AxisMove::EPSILON_MM) {
        return std::nullopt;
    }
    return lift;
}

} // namespace helix
