// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/// @file nozzle_renderer_creality_k1.h
/// @brief Creality K1 series toolhead renderer

#pragma once

#include "lvgl/lvgl.h"

#include <optional>

/// @brief Draw Creality K1 toolhead
///
/// Metallic silver-gray body with large circular fan, sloped upper section,
/// and chamfered V-cut bottom. Proportions ~1:1.2 W:H.
///
/// @param layer LVGL draw layer
/// @param cx Center X position
/// @param cy Center Y position (center of entire print head)
/// @param filament Loaded filament color (tints the nozzle tip), or nullopt when unloaded
/// @param scale_unit Base scaling unit
/// @param opa Opacity (default LV_OPA_COVER)
void draw_nozzle_creality_k1(lv_layer_t* layer, int32_t cx, int32_t cy,
                             std::optional<lv_color_t> filament, int32_t scale_unit,
                             lv_opa_t opa = LV_OPA_COVER);
