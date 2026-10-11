// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file gcode_ghost_mode.h
 * @brief How the ghost pass draws the layers above the one on screen.
 *
 * Separate from gcode_ghost_sampling.h, which decides *which* layers that pass
 * visits. This is the appearance half, and it is shared because the mode is a
 * viewer-level setting: ui_gcode_viewer.cpp maps its int wire value
 * (0=Dimmed, 1=Stipple) onto this enum and hands it to the renderer.
 */

#pragma once

#include <cstdint>

namespace helix {
namespace gcode {

/**
 * @brief Ghost layer rendering mode (for print progress visualization).
 *
 * Only the 3D renderer reads this. The 2D renderer draws its own ghost, a
 * faded whole-model buffer, whatever the mode.
 */
enum class GhostRenderMode : uint8_t {
    Dimmed = 0, ///< Reduce opacity of unprinted layers
    Stipple = 1 ///< Use stipple pattern for unprinted layers
};

/// Mode used when nothing has set one explicitly.
inline constexpr GhostRenderMode DEFAULT_GHOST_RENDER_MODE = GhostRenderMode::Stipple;

} // namespace gcode
} // namespace helix
