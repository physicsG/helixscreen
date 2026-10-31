// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "glass_shading.h"
#include "lvgl/lvgl.h"

/**
 * @file glass_painter.h
 * @brief Draws glass by rewriting a widget's fill and border draw tasks as they are queued
 *
 * Styles are left alone: each opaque flat fill becomes a gradient derived from the fill's own
 * resolved color, so a locally set bg color still shows, shaded. A widget's family comes from
 * the shared theme style it carries, or from a glass_mark() marker. Main thread only.
 */

namespace helix::ui {

/// The glass family of `obj`'s `part`: a glass_mark() marker first, then what the widget's
/// class makes of the part (a slider's track is Recessed, its knob Raised), then the shared
/// theme style the part carries, otherwise None.
GlassFamily glass_family_of(const lv_obj_t* obj, lv_part_t part);

/// Gives `obj` an explicit family, overriding what its styles say. None exempts the whole
/// widget (a color that identifies something, such as a filament swatch).
void glass_mark(lv_obj_t* obj, GlassFamily family);

/// Starts rewriting `obj`'s draw tasks. Idempotent; labels are never attached.
void glass_attach(lv_obj_t* obj);

/// Stops rewriting `obj`'s draw tasks. Clears LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS only when
/// glass_attach() set it, so another user of the flag keeps it.
void glass_detach(lv_obj_t* obj);

/// Whether glass_attach() is in effect on `obj`.
bool glass_attached(const lv_obj_t* obj);

/// Turns a flat fill into the family's gradient over the fill's own color. `fill_opa` is the
/// part's own bg_opa: a translucent fill is left alone, while an opaque fill drawn faded because
/// its whole widget is (a disabled button) is shaded and keeps the fade. A fill that already
/// carries a gradient is left alone.
void glass_rewrite_fill(lv_draw_fill_dsc_t* dsc, lv_opa_t fill_opa, GlassFamily f, bool dark,
                        bool pressed);

/// Recolors a border as the family's rim over `base`, the part's bg color.
void glass_rewrite_border(lv_draw_border_dsc_t* dsc, GlassFamily f, lv_color_t base, bool dark);

} // namespace helix::ui
