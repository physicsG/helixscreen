// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/// @file nozzle_renderer_bambu.h
/// @brief Bambu-style metallic gray toolhead renderer

#pragma once

#include "lvgl/lvgl.h"

#include <optional>

/// @brief Draw Bambu-style metallic gray print head
///
/// Creates a 3D isometric view of a print head with:
/// - Heater block (main body with gradient shading)
/// - Large circular fan duct
/// - Tapered nozzle tip
///
/// @param layer LVGL draw layer
/// @param cx Center X position
/// @param cy Center Y position (center of entire print head)
/// @param filament Loaded filament color (tints the nozzle tip), or nullopt when unloaded
/// @param scale_unit Base scaling unit (typically from theme space_md)
void draw_nozzle_bambu(lv_layer_t* layer, int32_t cx, int32_t cy,
                       std::optional<lv_color_t> filament, int32_t scale_unit,
                       lv_opa_t opa = LV_OPA_COVER);
