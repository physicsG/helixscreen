// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Pure geometry of the Toolhead widget's canvas: the top-down plate, the crosshair on
// it, and the Z bar beside it.
//
// Free of LVGL so it is unit-testable without a display. The widget's draw hook feeds
// it the resolved bed, the position and the canvas box, and draws the rectangles it
// returns. Coordinates are canvas-relative pixels.

#include "bed_coord_mapper.h"
#include "bed_dimensions.h"

#include <algorithm>
#include <cmath>

namespace helix {

struct ToolheadPlateInput {
    int box_w = 0; ///< the canvas the plate and the Z bar share
    int box_h = 0;
    int inset_px = 0; ///< kept clear on every side
    int bar_w_px = 0; ///< the Z bar's width
    int gap_px = 0;   ///< between the plate and the Z bar
    BedDimensions bed{235.0f, 235.0f, 0.0f, 0.0f};
    bool circular = false;  ///< a delta draws a round plate
    bool bed_moves = false; ///< Z moves the bed, so the Z bar's fill hangs from the top
    bool homed = false;     ///< all three axes; an unhomed position means nothing
    /// Pack the plate and bar against the left edge instead of centring them, for a
    /// canvas that shares its row with the axis values.
    bool align_start = false;
    float x_mm = 0.0f;
    float y_mm = 0.0f;
    float z_mm = 0.0f;
    bool has_z_range = false;
    float z_min_mm = 0.0f;
    float z_max_mm = 0.0f;
};

struct ToolheadPlateGeometry {
    PixelRect plate{};
    bool circular = false;
    PixelRect z_bar{};
    bool has_room = false; ///< false when the box is too small to draw a plate at all
    bool show_crosshair = false;
    float cross_x = 0.0f;
    float cross_y = 0.0f;
    /// How much of the Z bar is filled, 0..1; the fill hangs from the top when
    /// `fill_from_top` and rises from the bottom otherwise.
    float z_fraction = 0.0f;
    bool fill_from_top = false;
};

[[nodiscard]] inline ToolheadPlateGeometry plan_toolhead_plate(const ToolheadPlateInput& in) {
    ToolheadPlateGeometry g;
    g.circular = in.circular;
    g.fill_from_top = in.bed_moves;

    const int reserved = in.bar_w_px + in.gap_px;
    const int view_w = in.box_w - 2 * in.inset_px - reserved;
    const int view_h = in.box_h - 2 * in.inset_px;
    if (view_w <= 0 || view_h <= 0)
        return g;
    g.has_room = true;

    const BedCoordMapper mapper(in.bed.w_mm, in.bed.h_mm, view_w, view_h, in.bed.origin_x,
                                in.bed.origin_y);
    // Y is flipped by the mapper: the bed's far edge is the top of the canvas.
    const auto top_left = mapper.mm_to_px(in.bed.origin_x, in.bed.origin_y + in.bed.h_mm);
    const auto bottom_right = mapper.mm_to_px(in.bed.origin_x + in.bed.w_mm, in.bed.origin_y);
    const float plate_w = bottom_right.first - top_left.first;
    const float plate_h = bottom_right.second - top_left.second;

    // The plate and the Z bar sit together, centred unless packed to the left.
    const float group_w = plate_w + static_cast<float>(reserved);
    const float group_x = in.align_start ? static_cast<float>(in.inset_px)
                                         : (static_cast<float>(in.box_w) - group_w) / 2.0f;
    const float shift_x = group_x - top_left.first;
    const float shift_y = static_cast<float>(in.inset_px);
    g.plate = {top_left.first + shift_x, top_left.second + shift_y, plate_w, plate_h};
    g.z_bar = {g.plate.x + plate_w + static_cast<float>(in.gap_px), g.plate.y,
               static_cast<float>(in.bar_w_px), plate_h};

    if (!in.homed)
        return g;

    const float x = std::clamp(in.x_mm, in.bed.origin_x, in.bed.origin_x + in.bed.w_mm);
    const float y = std::clamp(in.y_mm, in.bed.origin_y, in.bed.origin_y + in.bed.h_mm);
    const auto cross = mapper.mm_to_px(x, y);
    g.show_crosshair = true;
    g.cross_x = cross.first + shift_x;
    g.cross_y = cross.second + shift_y;

    if (in.has_z_range && in.z_max_mm > in.z_min_mm) {
        g.z_fraction =
            std::clamp((in.z_mm - in.z_min_mm) / (in.z_max_mm - in.z_min_mm), 0.0f, 1.0f);
    }
    return g;
}

} // namespace helix
