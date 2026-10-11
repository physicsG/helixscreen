// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "bed_mesh_renderer.h" // For bed_mesh_point_3d_t, bed_mesh_view_state_t

/**
 * @file bed_mesh_projection.h
 * @brief 3D to 2D projection for bed mesh visualization
 *
 * Provides perspective projection from 3D world coordinates to 2D screen space
 * with rotation (Z-axis spin, X-axis tilt) and depth calculation for z-buffering.
 */

/**
 * @brief Project a 3D point to 2D screen space with depth
 *
 * Applies 3D rotation (Z-axis spin, X-axis tilt), camera translation, and
 * perspective projection to convert world coordinates to screen coordinates.
 *
 * Uses cached trigonometric values from view_state for performance.
 *
 * @param x World X coordinate
 * @param y World Y coordinate
 * @param z World Z coordinate
 * @param canvas_width Canvas width in pixels
 * @param canvas_height Canvas height in pixels
 * @param view View/camera state (angles, scale, centering offset)
 * @return Projected point with screen_x, screen_y, and depth
 */
bed_mesh_point_3d_t bed_mesh_projection_project_3d_to_2d(double x, double y, double z,
                                                         int canvas_width, int canvas_height,
                                                         const bed_mesh_view_state_t* view);

/// Pan the magnified view by (dx, dy) canvas px. No-op at BED_MESH_ZOOM_MIN.
// NAMESPACE_OK: joins this header's global bed_mesh_projection_* free-function API
void bed_mesh_projection_pan(bed_mesh_view_state_t* view, double dx, double dy);

/// Zoom by factor about canvas-local (anchor_x, anchor_y), clamped to
/// [BED_MESH_ZOOM_MIN, BED_MESH_ZOOM_MAX]. Reaching the minimum resets pan.
// NAMESPACE_OK: joins this header's global bed_mesh_projection_* free-function API
void bed_mesh_projection_zoom_at(bed_mesh_view_state_t* view, double factor, double anchor_x,
                                 double anchor_y, int canvas_width, int canvas_height);

/// Back to the fitted view: zoom 1, no pan.
// NAMESPACE_OK: joins this header's global bed_mesh_projection_* free-function API
void bed_mesh_projection_reset_zoom(bed_mesh_view_state_t* view);
