// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The layers every top-down bed view draws inside its plate: the reference grid and the
// outlines of a print's objects. The Motion panel's Bed tab and the home Toolhead widget
// both draw through draw_bed_plate_layers(), so the two cannot drift apart.
//
// plan_bed_plate_layers() is pure (no LVGL) and unit-tested; draw_bed_plate_layers() only
// strokes what it returns.

#include "bed_coord_mapper.h"
#include "bed_occupancy.h"

#include <lvgl.h>
#include <vector>

namespace helix {

/// The smallest on-screen spacing, in pixels, at which the 50mm grid is drawn. Below it the
/// lines crowd the plate and the grid is left out.
inline constexpr float kMinGridSpacingPx = 12.0f;

struct BedPlateLayersInput {
    float x_min = 0.0f, x_max = 0.0f; ///< the plate in printer millimetres
    float y_min = 0.0f, y_max = 0.0f;
    bool circular = false;
    std::vector<BedObjectOutline> outlines;
};

struct PxSegment {
    float x1, y1, x2, y2;
};

struct PxPoint {
    float x, y;
};

struct BedPlateLayers {
    std::vector<PxSegment> grid; ///< empty when the spacing is below kMinGridSpacingPx
    std::vector<std::vector<PxPoint>> outlines; ///< closed polygons, viewport pixels
};

/// Map the grid and outlines into the viewport @p mapper was built for. Outline points are
/// clamped to the plate; an outline with fewer than three points is dropped.
[[nodiscard]] BedPlateLayers plan_bed_plate_layers(const BedPlateLayersInput& in,
                                                   const BedCoordMapper& mapper);

struct BedPlateStyle {
    lv_color_t grid_color;
    lv_opa_t grid_opa = LV_OPA_30;
    lv_color_t outline_color;
    lv_opa_t outline_fill_opa = LV_OPA_20;
    int outline_width_px = 1;
};

/// Stroke @p layers onto @p layer, offset by @p origin (the viewport's top-left in screen
/// coordinates). Call from a DRAW_MAIN / DRAW_MAIN_END hook.
void draw_bed_plate_layers(lv_layer_t* layer, const lv_point_t& origin,
                           const BedPlateLayers& layers, const BedPlateStyle& style);

} // namespace helix
