// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/// @file nozzle_renderer_stealthburner.h
/// @brief Voron Stealthburner toolhead renderer
///
/// Vector-drawn Stealthburner print head using LVGL polygon primitives.
/// Automatically uses Voron red when no filament color is loaded.

#pragma once

#include "lvgl/lvgl.h"

#include <optional>

/// @brief Draw Voron Stealthburner print head
///
/// Creates a detailed vector rendering of the Stealthburner toolhead with:
/// - Dark housing outline
/// - Faceted body panels with 3D shading effect
/// - Top motor recess circle
/// - Bottom fan circle
/// - Voron logo stripes
///
/// The toolhead is colored with the loaded filament color when printing,
/// or defaults to Voron red (#D11D1D) when no filament is loaded.
///
/// @param layer LVGL draw layer
/// @param cx Center X position
/// @param cy Center Y position (center of entire print head)
/// @param filament Loaded filament color (tints the nozzle tip), or nullopt when unloaded
/// @param scale_unit Base scaling unit (typically from theme space_md)
void draw_nozzle_stealthburner(lv_layer_t* layer, int32_t cx, int32_t cy,
                               std::optional<lv_color_t> filament, int32_t scale_unit,
                               lv_opa_t opa = LV_OPA_COVER);
