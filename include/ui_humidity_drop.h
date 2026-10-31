// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "humidity_drop_geometry.h"
#include "lvgl/lvgl.h"

/**
 * @file ui_humidity_drop.h
 * @brief <humidity_drop>: a water drop filled to the level of a humidity reading.
 *
 * Drop-in for `<icon src="water">` wherever a humidity reading sits beside one:
 *
 *   <humidity_drop bind_value="chamber_humidity" size="sm"/>
 *
 * - `bind_value`: int subject in tenths of a percent (455 = 45.5%); below zero
 *   means no reading, drawn as a faint outline with no fill.
 * - `size`: xs..xl, the same box an icon of that size takes. The box follows the
 *   widget's text font, so `<bind_tile_rung ladder="icon">` re-faces it as it
 *   does an icon. An explicit `width`/`height` overrides the font.
 *
 * Every colour derives from the theme's `info` token (the water) and its
 * screen background, read at each draw, so a theme or mode change re-tints live. With animations
 * on, a partial fill's surface drifts on one shared slow tick that redraws only drops on screen.
 * Geometry and rasterization: humidity_drop_geometry.h.
 */
namespace helix::ui {

void register_humidity_drop_widget();

/// Set a drop's reading directly, for a drop with no subject to bind (tenths of
/// a percent; below zero = no reading).
void humidity_drop_set_value(lv_obj_t* obj, int humidity_x10);

/// Whether @p obj is a <humidity_drop>: like an icon, it draws a glyph sized by its text font.
bool is_humidity_drop(const lv_obj_t* obj);

/// The reading a drop shows; humidity_drop::kUnknown when it has none.
int humidity_drop_value(lv_obj_t* obj);

/// The colours drops draw in under the current theme.
humidity_drop::DropColors humidity_drop_colors();

/// The shared wave phase, in radians. Moves only while animations are on.
float humidity_drop_phase();

/// One step of the shared wave timer: advance the phase and redraw every drop on
/// screen with a partial fill. Does nothing while animations are off.
void humidity_drop_tick();

} // namespace helix::ui
