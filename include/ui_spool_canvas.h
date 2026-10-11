// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

/**
 * @file ui_spool_canvas.h
 * @brief Pseudo-3D filament spool visualization using LVGL canvas
 *
 * Creates a visually appealing 3D-style spool with approximately 20 degrees
 * rotation, showing a front face ellipse plus visible side depth.
 *
 * XML usage:
 * @code{.xml}
 * <spool_canvas color="0xFF5722" fill_level="0.75" size="64"/>
 * @endcode
 *
 * Properties:
 *   - color: Filament color as hex (e.g., "0xFF5722")
 *   - fill_level: Amount of filament 0.0 (empty) to 1.0 (full)
 *   - size: Canvas size in pixels (default 64)
 */

void ui_spool_canvas_register(void);

/**
 * @brief Create a spool canvas programmatically
 *
 * Alternative to XML creation for use in C++ code.
 *
 * @param parent Parent LVGL object
 * @param size Canvas size in pixels (default 64 if 0)
 * @return Created canvas object, or NULL on failure
 */
lv_obj_t* ui_spool_canvas_create(lv_obj_t* parent, int32_t size);

void ui_spool_canvas_set_color(lv_obj_t* canvas, lv_color_t color);
void ui_spool_canvas_set_fill_level(lv_obj_t* canvas, float fill_level);
void ui_spool_canvas_set_size(lv_obj_t* canvas, int32_t size);
void ui_spool_canvas_redraw(lv_obj_t* canvas);
float ui_spool_canvas_get_fill_level(lv_obj_t* canvas);
lv_color_t ui_spool_canvas_get_color(lv_obj_t* canvas);

/**
 * @brief Clear the internal render cache.
 *
 * Rendered pixel buffers are cached keyed on (color, fill_bucket, size). The
 * cache uses theme-derived colors (flange, hub) captured at render time; if
 * the theme changes, cached entries would be stale. Call this on theme
 * changes to force re-render.
 */
void ui_spool_canvas_invalidate_cache(void);

#ifdef __cplusplus
}

namespace helix::ui {
bool reduced_effects();

/**
 * @brief Create a glow layer: an image the caller places behind a spool,
 *        centered on it and sized like it.
 */
lv_obj_t* spool_glow_create(lv_obj_t* parent);

/**
 * @brief Light the glow for a silhouette of @p size px and keep it current.
 *
 * The silhouette is @p spool_canvas's own alpha (3D style), or a disc of
 * diameter @p size when it is null (flat style). The layer stays bound to the
 * spool: it repaints when the spool renders a new shape, and stays empty until
 * the spool's first render. Capable hardware gets a soft accent halo plus a
 * lighter tight rim; @p simple a 2 px solid outline. The image overhangs the
 * object by the glow margin, reported as ext draw size, so layout never moves.
 * One image per (shape, accent, simple) is shared by every layer showing it,
 * from a small cache of its own. Calling again re-reads the theme accent.
 */
void spool_glow_paint(lv_obj_t* glow, lv_obj_t* spool_canvas, int32_t size,
                      bool simple = reduced_effects());

/// Unlight the layer and drop its image reference.
void spool_glow_clear(lv_obj_t* glow);
} // namespace helix::ui
#endif
