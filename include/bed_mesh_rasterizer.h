// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file bed_mesh_rasterizer.h
 * @brief Triangle fills for bed mesh visualization
 *
 * Take lv_color_t vertex colors and delegate to PixelBuffer's scanline fills.
 * No LVGL draw calls, so they run on the render thread.
 */

#include <lvgl/lvgl.h>

namespace helix {
namespace mesh {

/// Default opacity for mesh triangle surfaces (100% - fully opaque)
constexpr lv_opa_t MESH_TRIANGLE_OPACITY = LV_OPA_COVER;

class PixelBuffer;

/**
 * @brief Fill triangle with solid color into a pixel buffer
 *
 * @param buf Target pixel buffer
 * @param x1, y1 First vertex coordinates
 * @param x2, y2 Second vertex coordinates
 * @param x3, y3 Third vertex coordinates
 * @param color Fill color (r/g/b extracted from lv_color_t)
 * @param opacity Fill opacity (default: MESH_TRIANGLE_OPACITY)
 */
void fill_triangle_solid(PixelBuffer& buf, int x1, int y1, int x2, int y2, int x3, int y3,
                         lv_color_t color, lv_opa_t opacity = MESH_TRIANGLE_OPACITY);

/**
 * @brief Fill triangle with per-vertex color gradient into a pixel buffer
 *
 * @param buf Target pixel buffer
 * @param x1, y1, c1 First vertex position and color
 * @param x2, y2, c2 Second vertex position and color
 * @param x3, y3, c3 Third vertex position and color
 * @param opacity Fill opacity (default: MESH_TRIANGLE_OPACITY)
 */
void fill_triangle_gradient(PixelBuffer& buf, int x1, int y1, lv_color_t c1, int x2, int y2,
                            lv_color_t c2, int x3, int y3, lv_color_t c3,
                            lv_opa_t opacity = MESH_TRIANGLE_OPACITY);

} // namespace mesh
} // namespace helix
