// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/// @file nozzle_renderer_anthead.h
/// @brief Micron AntHead toolhead renderer (image-based)
///
/// Renders the Micron AntHead toolhead from a pre-rasterized ARGB8888 image.
/// Unlike polygon-based renderers, this uses LVGL image draw with scaling.

#pragma once

#include "lvgl/lvgl.h"

#include <optional>

/// @brief The AntHead image (100x163 ARGB8888), decoded on first call
/// @return nullptr if the asset could not be decoded. Not built for ESP32.
const lv_draw_buf_t* anthead_image();

/// @brief Draw Micron AntHead toolhead
///
/// Renders the AntHead image centered at (cx, cy), scaled proportionally
/// to the given scale_unit (matching the polygon renderers' convention).
/// ESP32 does not ship the image and draws the default toolhead glyph instead.
///
/// @param layer LVGL draw layer
/// @param cx Center X position
/// @param cy Center Y position
/// @param filament Unused by the image (it has fixed colors)
/// @param scale_unit Base scaling unit (typically from theme space_md)
/// @param opa Opacity (default LV_OPA_COVER)
void draw_nozzle_anthead(lv_layer_t* layer, int32_t cx, int32_t cy,
                         std::optional<lv_color_t> filament, int32_t scale_unit,
                         lv_opa_t opa = LV_OPA_COVER);
