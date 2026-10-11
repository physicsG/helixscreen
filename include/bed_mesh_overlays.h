// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "bed_mesh_coordinate_transform.h" // For WallBounds
#include "bed_mesh_renderer.h"             // For bed_mesh_renderer_t, bed_mesh_view_state_t

#include <lvgl/lvgl.h>

/**
 * @file bed_mesh_overlays.h
 * @brief Grid lines, axes, labels and heatmap overlays for bed mesh visualization
 *
 * The render thread draws the wireframe and reference grids into a PixelBuffer.
 * Text and the 2D heatmap's touch overlay need LVGL's font engine, so the
 * widget's draw callback draws those on the main thread on top of the blitted frame.
 */

namespace helix {
namespace mesh {

class PixelBuffer;

/**
 * @brief Printer-bed extent of the reference grids and its world transform
 *
 * The one place that decides the grid extent: the bed bounds once
 * bed_mesh_renderer_set_bounds() has run, else the mesh index grid at BED_MESH_SCALE.
 */
struct BedExtent {
    double x_min_mm, x_max_mm, y_min_mm, y_max_mm; ///< Bed range in printer mm
    double center_x, center_y;                     ///< Bed center in printer mm
    double coord_scale;                            ///< World units per mm
    double half_width, half_height;                ///< Bed half-size in world units
    WallBounds walls;                              ///< Floor/ceiling at the current z_scale
};

BedExtent compute_bed_extent(const bed_mesh_renderer_t* renderer);

/**
 * @brief Cell layout of the 2D heatmap within the canvas (canvas-local pixels)
 *
 * N probe points per axis give N-1 cells. `valid` is false below a 2x2 mesh.
 */
struct HeatmapLayout {
    bool valid;
    int grid_x, grid_y;   ///< Top-left of the first cell
    int cell_w, cell_h;   ///< Cell size, at least 1px
    int cells_x, cells_y; ///< Cell count per axis
};

HeatmapLayout compute_heatmap_layout(int rows, int cols, int canvas_width, int canvas_height);

/**
 * @brief Render axis labels (X, Y, Z indicators) on the main thread
 *
 * X and Y sit at the middle of their axis, just outside the grid edge; Z sits
 * above the ceiling of the front-left corner.
 */
void render_axis_labels(lv_layer_t* layer, const bed_mesh_renderer_t* renderer, int canvas_width,
                        int canvas_height);

/**
 * @brief Render numeric tick labels on X, Y, and Z axes on the main thread
 *
 * Millimeter labels at the reference-grid spacing along X and Y, and probe
 * heights (with the Z display offset added back) along Z.
 */
void render_numeric_axis_ticks(lv_layer_t* layer, const bed_mesh_renderer_t* renderer,
                               int canvas_width, int canvas_height);

/**
 * @brief Draw a single axis tick label at the given screen position
 *
 * @param use_decimals If true, formats with 2 decimal places (for Z-axis mm values)
 *                     If false, formats as whole number (for X/Y axis values)
 */
void draw_axis_tick_label(lv_layer_t* layer, lv_draw_label_dsc_t* label_dsc, int screen_x,
                          int screen_y, int offset_x, int offset_y, double value, int canvas_width,
                          int canvas_height, bool use_decimals = false);

/**
 * @brief Draw the 2D heatmap's border, touched-cell highlight and Z tooltip
 *
 * Runs on the main thread over the blitted heatmap frame.
 *
 * @param layout Layout of the frame being shown
 * @param offset_x, offset_y Widget's absolute screen position
 */
void render_heatmap_overlay(lv_layer_t* layer, const bed_mesh_renderer_t* renderer,
                            const HeatmapLayout& layout, int offset_x, int offset_y);

/**
 * @brief Render grid lines on mesh surface into a pixel buffer
 *
 * @param line_r, line_g, line_b Grid line color (pre-fetched from theme)
 */
void render_grid_lines(PixelBuffer& buf, const bed_mesh_renderer_t* renderer, int canvas_width,
                       int canvas_height, uint8_t line_r, uint8_t line_g, uint8_t line_b);

/**
 * @brief Render reference grids (floor, back wall, left wall) into a pixel buffer
 *
 * Uses the bed extent, so the mesh floats inside. Draw before the mesh surface
 * so the surface occludes it.
 *
 * @param line_r, line_g, line_b Grid line color (pre-fetched from theme)
 */
void render_reference_grids(PixelBuffer& buf, const bed_mesh_renderer_t* renderer, int canvas_width,
                            int canvas_height, uint8_t line_r, uint8_t line_g, uint8_t line_b);

} // namespace mesh
} // namespace helix
