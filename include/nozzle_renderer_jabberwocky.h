// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/// @file nozzle_renderer_jabberwocky.h
/// @brief JabberWocky V80 toolhead renderer
///
/// Vector-drawn JabberWocky V80 print head using LVGL polygon primitives.
/// Distinctive body with blue accent features (JabberWocky signature color).

#pragma once

#include "lvgl/lvgl.h"

#include <optional>

/// @brief Draw JabberWocky V80 print head
///
/// Creates a vector rendering of the JabberWocky V80 toolhead with:
/// - Dark body housing with beveled surfaces
/// - Gray surface detail and mechanism features
/// - Blue accent features (JabberWocky signature color)
/// - Green accent highlights
/// - Copper/orange nozzle assembly
/// - Tapered nozzle tip with filament color
///
/// The toolhead body is always dark with blue (#3890C0) accents.
/// Nozzle tip shows filament color when loaded.
///
/// @param layer LVGL draw layer
/// @param cx Center X position
/// @param cy Center Y position (center of entire print head)
/// @param filament Loaded filament color (tints the nozzle tip), or nullopt when unloaded
/// @param scale_unit Base scaling unit (typically from theme space_md)
/// @param opa Opacity (default LV_OPA_COVER)
void draw_nozzle_jabberwocky(lv_layer_t* layer, int32_t cx, int32_t cy,
                             std::optional<lv_color_t> filament, int32_t scale_unit,
                             lv_opa_t opa = LV_OPA_COVER);
