// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file ui_tile_rung.h
 * @brief The face each home-tile part draws in at each size rung
 *
 * A sizing tile publishes one rung index (TileSizing); every part of the tile
 * binds its face to that index here. The ladders are the single table both
 * sides read: TileSizing measures a rung in these faces, and <bind_tile_rung>
 * draws it in them, so what is measured is what is drawn.
 *
 * @threading Main thread only
 */

#include "lvgl/lvgl.h"
#include "src/ui/panel_widgets/tile_layout.h"

namespace helix::ui {

/// Which part of a tile an object draws, and so which face ladder it follows.
enum class TileLadder : int {
    Icon = 0,
    Value = 1,
    Label = 2,
    /// The disc behind a badged glyph: the icon ladder, drawn as a square of
    /// tile_disc_edge() of that face instead of as a face.
    Disc = 3,
};

/// The face a tile part draws in at a rung, and the scale it is drawn at.
///
/// Every rung but the icon's xxl is a theme token at 1x. The icon's xxl rung is
/// a size (#tile_icon_xxl_size): it draws in the largest MDI face this build
/// links at or below that size, scaled up to reach it, never past kTileMaxScale.
struct TileFace {
    const lv_font_t* font = nullptr;
    int32_t scale = LV_SCALE_NONE;

    /// @p unscaled px at this face's scale.
    int px(int unscaled) const {
        return static_cast<int>(unscaled * scale / LV_SCALE_NONE);
    }
};

#if defined(HELIX_PLATFORM_ESP32)
/// A scaled glyph renders through a layer buffer, and the ESP32 image has no
/// RAM to spare for one per tile, so there the xxl rung draws the largest real
/// face as it is.
inline constexpr int32_t kTileMaxScale = LV_SCALE_NONE;
#else
/// A bitmap glyph drawn past twice its size stops reading as the glyph.
inline constexpr int32_t kTileMaxScale = 2 * LV_SCALE_NONE;
#endif

/// A scaled glyph renders through a layer buffer that is allocated and redrawn
/// on every frame anything invalidates it, so a glyph that animates (a heater's
/// pulse) costs that every animation frame. It draws the largest real face at
/// 1x instead.
inline constexpr int32_t kTileAnimatedMaxScale = LV_SCALE_NONE;

/// The scale that draws a @p face_px face at @p target_px, capped at
/// @p max_scale and never below 1x.
inline int32_t tile_xxl_scale(int target_px, int face_px, int32_t max_scale) {
    if (face_px <= 0) {
        return LV_SCALE_NONE;
    }
    const int32_t reach = static_cast<int32_t>(target_px) * LV_SCALE_NONE / face_px;
    return reach < LV_SCALE_NONE ? LV_SCALE_NONE : (reach > max_scale ? max_scale : reach);
}

/// The face @p ladder draws in at @p rung, its scale capped at @p max_scale.
/// Out-of-range rungs clamp to the ladder's ends.
TileFace tile_rung_face(TileLadder ladder, int rung, int32_t max_scale = kTileMaxScale);

/// The rung a glyph asked for at @p rung draws at, by size: an xxl glyph that
/// draws @p xxl_px, no larger than the xl face's @p xl_px, is an xl glyph.
/// Anything sized off the glyph (a count badge) follows this rung, not the
/// requested one.
inline int tile_drawn_rung(int rung, int xl_px, int xxl_px) {
    return rung >= kTileRungs - 1 && xxl_px <= xl_px ? kTileRungs - 2 : rung;
}

/// tile_drawn_rung() for this tier's faces, the xxl glyph capped at @p max_scale.
int tile_drawn_rung(int rung, int32_t max_scale = kTileMaxScale);

/// The theme font token @p ladder names at @p rung; for the icon's xxl rung,
/// the xl token it grows from.
const char* tile_rung_font_token(TileLadder ladder, int rung);

/// The layout box a glyph drawn in @p face takes: the face's glyph box grown to
/// its scale on each axis. What TileSizing measures and what the binding pads
/// a scaled glyph to.
struct TileGlyphBox {
    int w = 0;
    int h = 0;
};
TileGlyphBox tile_glyph_box(const TileFace& face);

/// Edge of the disc a badged glyph in @p icon_face sits in, in px.
int tile_disc_edge(const TileFace& icon_face);

/// Keep @p obj's face on @p ladder at the rung @p subject holds, shifted by
/// @p offset rungs. The face is resolved from the current tier's token each
/// time the rung changes. @p one_line also holds the object to one line of that
/// face, which a long_mode="dots" label needs to ellipsize rather than wrap.
/// @p animated caps the glyph at kTileAnimatedMaxScale.
void bind_tile_rung(lv_obj_t* obj, lv_subject_t* subject, TileLadder ladder, int offset = 0,
                    bool one_line = false, bool animated = false);

/// Register `<bind_tile_rung ladder="icon|value|label|disc" subject="..." offset="0"
/// one_line="false" animated="false"/>`, a child element of any widget. An empty subject installs
/// no binding, so a component used outside a sizing tile keeps its authored face.
void register_tile_rung_binding();

} // namespace helix::ui
