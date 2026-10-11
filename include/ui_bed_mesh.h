// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "bed_mesh_renderer.h" // For BedMeshRenderMode and the renderer-owned camera state
#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bed mesh canvas dimensions
#define BED_MESH_CANVAS_WIDTH 600
#define BED_MESH_CANVAS_HEIGHT 400

// Camera angle limits and defaults live with the renderer that owns them:
// BED_MESH_ANGLE_X_MIN/MAX and BED_MESH_DEFAULT_ANGLE_X/Z in bed_mesh_renderer.h

/**
 * @brief Register <bed_mesh> widget with LVGL XML system
 *
 * Creates a canvas widget (600×400 RGB888) optimized for 3D bed mesh rendering.
 * Automatically allocates buffer memory and renderer in create handler.
 *
 * Usage in XML:
 * @code{.xml}
 * <bed_mesh name="my_canvas" width="600" height="400"/>
 * @endcode
 */
void ui_bed_mesh_register(void);

/**
 * @brief Set mesh data for rendering
 *
 * Updates the renderer with new mesh height data. Mesh layout is row-major:
 * - mesh[row][col] where row = Y-axis (front to back)
 * - col = X-axis (left to right)
 * - values are absolute Z heights from printer bed
 *
 * @param canvas The bed_mesh canvas widget
 * @param mesh 2D array of height values (row-major)
 * @param rows Number of rows in mesh
 * @param cols Number of columns in mesh
 * @return true on success, false on error (NULL pointer, invalid dimensions)
 */
bool ui_bed_mesh_set_data(lv_obj_t* canvas, const float* const* mesh, int rows, int cols);

/**
 * @brief Set coordinate bounds for bed and mesh
 *
 * The bed bounds define the full print bed area (used for grid/walls).
 * The mesh bounds define where probing occurred (mesh is rendered within these).
 * Call this AFTER set_data() to position the mesh correctly within the bed.
 *
 * Works with any printer origin convention (corner at 0,0 or center at origin).
 *
 * @param canvas The bed_mesh canvas widget
 * @param bed_x_min Full bed minimum X coordinate
 * @param bed_x_max Full bed maximum X coordinate
 * @param bed_y_min Full bed minimum Y coordinate
 * @param bed_y_max Full bed maximum Y coordinate
 * @param mesh_x_min Mesh probe area minimum X coordinate
 * @param mesh_x_max Mesh probe area maximum X coordinate
 * @param mesh_y_min Mesh probe area minimum Y coordinate
 * @param mesh_y_max Mesh probe area maximum Y coordinate
 */
void ui_bed_mesh_set_bounds(lv_obj_t* canvas, double bed_x_min, double bed_x_max, double bed_y_min,
                            double bed_y_max, double mesh_x_min, double mesh_x_max,
                            double mesh_y_min, double mesh_y_max);

/**
 * @brief Force redraw of mesh visualization
 *
 * Asks the render thread for a new frame; a no-op while rendering is off.
 *
 * @param canvas The bed_mesh canvas widget
 */
void ui_bed_mesh_redraw(lv_obj_t* canvas);

/**
 * @brief Evaluate render mode based on FPS history
 *
 * Should be called when the bed mesh panel becomes visible (panel entry).
 * Mode evaluation only happens on panel entry, never during viewing,
 * to prevent jarring mode switches while the user is interacting.
 *
 * @param canvas The bed_mesh canvas widget
 */
void ui_bed_mesh_evaluate_render_mode(lv_obj_t* canvas);

/**
 * @brief Set render mode
 *
 * @param canvas The bed_mesh canvas widget
 * @param mode Render mode to use (AUTO, FORCE_3D, or FORCE_2D)
 */
void ui_bed_mesh_set_render_mode(lv_obj_t* canvas, helix::BedMeshRenderMode mode);

/**
 * @brief Show or hide the zero reference plane
 *
 * The zero plane is a translucent reference surface at Z=0 that intersects
 * the mesh, showing where the nozzle touches the bed. Parts of the mesh
 * above the plane obscure it; parts below are visible through the plane.
 *
 * @param canvas The bed_mesh canvas widget
 * @param visible true to show the zero plane, false to hide it
 */
void ui_bed_mesh_set_zero_plane_visible(lv_obj_t* canvas, bool visible);

/**
 * @brief Set Z display offset for axis labels and tooltips
 *
 * When mesh data is normalized, this offset is added back so displayed Z values
 * match the original probe heights.
 *
 * @param canvas The bed_mesh canvas widget
 * @param offset_mm The mean Z value that was subtracted during normalization
 */
void ui_bed_mesh_set_z_display_offset(lv_obj_t* canvas, double offset_mm);

/**
 * @brief Turn rendering on or off
 *
 * On, the widget runs a background render thread whose frames the DRAW_POST
 * callback blits. Off, the thread and both frame buffers are freed and the
 * widget draws nothing; the panel turns it off while hidden.
 *
 * @param widget The bed_mesh widget
 * @param enabled true to start the render thread, false to stop it
 */
void ui_bed_mesh_set_async_mode(lv_obj_t* widget, bool enabled);

/**
 * @brief Check if the render thread is running
 *
 * @param widget The bed_mesh widget
 * @return true if rendering is on, false otherwise
 */
bool ui_bed_mesh_is_async_mode(lv_obj_t* widget);

/**
 * @brief Check if the renderer has mesh data loaded
 *
 * @param widget The bed_mesh widget
 * @return true if mesh data has been set, false otherwise
 */
bool ui_bed_mesh_has_data(lv_obj_t* widget);

#ifdef __cplusplus
}
#endif
