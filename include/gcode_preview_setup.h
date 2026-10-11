// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file gcode_preview_setup.h
 * @brief Wiring shared by every G-code preview card.
 *
 * PrintStatusPanel and PrintSelectDetailView both present the same thing: a
 * card stacking a gradient, the G-code viewer, and a thumbnail, with a
 * translucent metadata strip along the bottom. The content of that strip
 * differs; the setup around it does not. These helpers are the single place
 * that setup lives, so a change to preview policy is one edit rather than two
 * that drift.
 *
 * @threading Main thread only — both helpers touch LVGL widgets.
 */

#pragma once

#include "lvgl.h"

#include <cstddef>
#include <cstdint>

namespace helix::ui {

/**
 * @brief Resolve and apply the preview's render mode.
 *
 * Reads the live ladder sources (RuntimeConfig, HELIX_GCODE_MODE, the persisted
 * display setting), routes them through
 * helix::gcode_viewer::decide_preview_mode(), applies the result to @p viewer
 * when the winning tier calls for it, and logs which tier won.
 *
 * @param viewer  The gcode_viewer widget. Null is tolerated (no-op).
 * @param log_tag Prefix for the log line, e.g. the panel's get_name().
 * @return true when the viewer will be used, false for Thumbnail Only.
 */
bool apply_preview_render_mode(lv_obj_t* viewer, const char* log_tag);

/**
 * @brief Is the G-code viewer used at all, per the live ladder?
 *
 * Same sources and same precedence as apply_preview_render_mode(), without
 * touching a widget or logging, so a caller can ask the question before it has
 * (or needs) a viewer. Thumbnail Only is the one answer of false, and it covers
 * the whole pipeline: no download, no layer index, no render pass.
 *
 * @return true when the viewer will be used, false for Thumbnail Only.
 */
bool preview_viewer_enabled();

/**
 * @brief Tell a preview which widget covers the bottom of it.
 *
 * The metadata strip is translucent and sits over the bottom of the preview in
 * some layouts and flush below it in others. Rather than every call site
 * guessing a shift, hand the viewer the strip: it measures the real overlap and
 * derives the offset, so the answer tracks breakpoints, orientation, and the
 * strip growing at runtime (an M117 adds a row).
 *
 * Pass a null @p occluder to clear. The viewer drops its reference when the
 * occluder is deleted, so the caller does not have to unwire on teardown.
 *
 * @param viewer   The gcode_viewer widget. Null is tolerated (no-op).
 * @param occluder Widget overlapping the viewer's bottom edge, or null.
 */
void set_preview_bottom_occluder(lv_obj_t* viewer, lv_obj_t* occluder);

/**
 * @brief May a cached download of a print's G-code be rendered as is?
 *
 * The cache is keyed by file name, so a same-name re-slice or a transfer cut
 * short by a crash leaves bytes that do not belong to the file now on the
 * server. A copy is usable only when it is non-empty and, when the server size
 * is known (@p expected_bytes > 0), exactly that size.
 */
bool preview_cache_is_current(size_t on_disk_bytes, uint64_t expected_bytes);

} // namespace helix::ui
