// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gcode_parser.h"

#include <glm/glm.hpp>

namespace helix::gcode {

// ============================================================================
// VIEW MODES
// ============================================================================

/// View mode for 2D projection of 3D toolpath data.
/// Shared by all renderers (layer renderer, thumbnail renderer, etc.)
enum class ViewMode {
    TOP_DOWN, ///< X/Y plane from above
    FRONT,    ///< Isometric-style: -45° horizontal + 45° elevation (default)
    ISOMETRIC ///< X/Y plane with isometric projection (45° rotation, Y compressed)
};

// ============================================================================
// PROJECTION CONSTANTS
// ============================================================================

/// Projection constants for FRONT view (-45° azimuth, 45° elevation).
/// Matching OrcaSlicer's default thumbnail camera (Camera.cpp zenit=45°, phi=45°).
namespace projection {

// 90° CCW pre-rotation (applied before horizontal rotation)
// new_x = -old_y, new_y = old_x

// Horizontal rotation: -45° (view from front-right corner)
constexpr float COS_H = 0.7071f;  // cos(45°)
constexpr float SIN_H = -0.7071f; // sin(-45°)

// Elevation angle: 45° looking down (matches OrcaSlicer thumbnail camera)
constexpr float COS_E = 0.7071f; // cos(45°)
constexpr float SIN_E = 0.7071f; // sin(45°)

// Isometric constants
constexpr float ISO_ANGLE = 0.7071f; // cos(45°)
constexpr float ISO_Y_SCALE = 0.5f;  // Y compression factor

} // namespace projection

// ============================================================================
// PROJECTION PARAMETERS
// ============================================================================

/// Parameters for world-to-screen coordinate transformation.
/// Captured as a snapshot for thread-safe rendering.
struct ProjectionParams {
    ViewMode view_mode = ViewMode::FRONT;
    float scale = 1.0f;
    float offset_x = 0.0f; ///< World-space center X
    float offset_y = 0.0f; ///< World-space center Y
    float offset_z = 0.0f; ///< World-space center Z (FRONT view only)
    int canvas_width = 0;
    int canvas_height = 0;
    float content_offset_y_percent =
        0.0f; ///< Vertical shift for UI overlap (layer renderer only, 0.0 for thumbnails)
};

// ============================================================================
// PROJECTION FUNCTIONS
// ============================================================================

/// Convert world coordinates to screen pixel coordinates.
///
/// This is the single source of truth for 2D projection across all renderers.
/// Supports TOP_DOWN, FRONT, and ISOMETRIC view modes.
///
/// @param params  Projection parameters (view mode, scale, offsets, canvas size)
/// @param x       World X coordinate (mm)
/// @param y       World Y coordinate (mm)
/// @param z       World Z coordinate (mm) - used by FRONT view
/// @return Screen coordinates in pixels (origin at top-left of canvas)
glm::ivec2 project(const ProjectionParams& params, float x, float y, float z = 0.0f);

/// Result of auto-fit computation.
struct AutoFitResult {
    float scale = 1.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float offset_z = 0.0f;
};

/// Compute projection scale and offsets to fit a bounding box within a canvas.
///
/// @param bb            Bounding box to fit (world coordinates, mm)
/// @param view_mode     Projection mode
/// @param canvas_width  Canvas width in pixels
/// @param canvas_height Canvas height in pixels
/// @param padding       Fractional padding around content (e.g. 0.05 = 5% each side)
/// @return Scale and offset parameters for use with project()
AutoFitResult compute_auto_fit(const AABB& bb, ViewMode view_mode, int canvas_width,
                               int canvas_height, float padding = 0.05f);

// ============================================================================
// DEPTH SHADING
// ============================================================================

/// Depth shading constants shared by all 2D renderers.
/// Bottom of model = darker, top = brighter. Back = slightly darker than front.
namespace depth_shading {

constexpr float MIN_BRIGHTNESS = 0.4f;        ///< Brightness at bottom (Z min)
constexpr float BRIGHTNESS_RANGE = 0.6f;      ///< Added at top (total = 0.4 + 0.6 = 1.0)
constexpr float BACK_FADE_MIN = 0.85f;        ///< Brightness at back (Y max)
constexpr float BACK_FADE_RANGE = 0.15f;      ///< Added at front (total = 0.85 + 0.15 = 1.0)
constexpr float FULL_GRADIENT_HEIGHT = 50.0f; ///< Objects shorter than this get compressed gradient

} // namespace depth_shading

/// Compute depth-based brightness factor for fake-3D shading in FRONT view.
///
/// Combines Z-height gradient (bottom=40%, top=100%) with subtle Y-depth fade
/// (front=100%, back=85%). Used by both the full-scene layer renderer and
/// per-object thumbnail renderer.
///
/// @param avg_z  Average Z of the segment
/// @param z_min  Minimum Z of the model/object bounding box
/// @param z_max  Maximum Z of the bounding box
/// @param avg_y  Average Y of the segment
/// @param y_min  Minimum Y of the model/object bounding box
/// @param y_max  Maximum Y of the bounding box
/// @return Brightness multiplier in [~0.34, 1.0]
inline float compute_depth_brightness(float avg_z, float z_min, float z_max, float avg_y,
                                      float y_min, float y_max) {
    constexpr float EPSILON = 0.001f;

    // Z-height: bottom=darker, top=brighter.
    // Short objects (<50mm) get a compressed gradient so they look more uniform.
    float brightness = depth_shading::MIN_BRIGHTNESS;
    float z_range = z_max - z_min;
    if (z_range > EPSILON) {
        float norm_z = (avg_z - z_min) / z_range;
        if (norm_z < 0.0f)
            norm_z = 0.0f;
        if (norm_z > 1.0f)
            norm_z = 1.0f;
        float height_scale = (z_range < depth_shading::FULL_GRADIENT_HEIGHT)
                                 ? z_range / depth_shading::FULL_GRADIENT_HEIGHT
                                 : 1.0f;
        float effective_range = depth_shading::BRIGHTNESS_RANGE * height_scale;
        float floor =
            depth_shading::MIN_BRIGHTNESS + depth_shading::BRIGHTNESS_RANGE - effective_range;
        brightness = floor + effective_range * norm_z;
    }

    // Y-depth: front (low Y) = 100%, back (high Y) = 85%
    float y_range = y_max - y_min;
    if (y_range > EPSILON) {
        float norm_y = (avg_y - y_min) / y_range;
        if (norm_y < 0.0f)
            norm_y = 0.0f;
        if (norm_y > 1.0f)
            norm_y = 1.0f;
        brightness *=
            depth_shading::BACK_FADE_MIN + depth_shading::BACK_FADE_RANGE * (1.0f - norm_y);
    }

    return brightness;
}

} // namespace helix::gcode
