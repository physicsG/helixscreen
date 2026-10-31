// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A widget that consumes drags: a press on it must not swipe the home carousel, and a hold
// on it must not enter home edit mode. lv_arc and lv_slider are drag surfaces by class; a
// plain lv_obj becomes one through mark_drag_surface(). Home edit mode and the fan control
// overlay ask is_drag_surface_hit() instead of naming widget classes.

#include <lvgl.h>

namespace helix::ui {

/// Marks an object as a drag surface. Held in a user flag so it costs no allocation.
inline constexpr lv_obj_flag_t kDragSurfaceFlag = LV_OBJ_FLAG_USER_1;

/// Flag @p obj as a drag surface and give it the input flags one needs: clickable, not
/// scrollable, no scroll chaining (so the press never reaches an ancestor's scroll), press
/// lock and no gesture bubbling.
void mark_drag_surface(lv_obj_t* obj);

/// Whether @p target or an ancestor up to and including @p stop_at is an lv_arc, an
/// lv_slider, or an object flagged by mark_drag_surface(). @p stop_at may be nullptr to
/// walk to the screen.
[[nodiscard]] bool is_drag_surface_hit(lv_obj_t* target, lv_obj_t* stop_at);

} // namespace helix::ui
