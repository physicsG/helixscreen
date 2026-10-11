// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

/**
 * @brief Register the severity_card widget with LVGL's XML system
 *
 * The severity_card widget provides automatic color styling based on
 * a semantic severity level. In XML, simply pass:
 *
 *   <severity_card severity="error" ...>
 *
 * The widget will:
 * - Set border-left color to the appropriate severity color
 * - Store severity for later use by finalize
 *
 * Available severity values: "error", "warning", "success", "info" (default)
 *
 * Must be called before any XML files using <severity_card> are registered.
 */
void ui_severity_card_register(void);

/**
 * @brief Finalize severity styling for children
 *
 * Call this after creating a severity_card via lv_xml_create to style
 * children (which don't exist during widget apply phase).
 *
 * The card's XML defines one hidden icon per severity (icon_info,
 * icon_success, icon_warning, icon_error); this unhides the matching one.
 *
 * @param obj The severity_card widget
 */
void ui_severity_card_finalize(lv_obj_t* obj);

#ifdef __cplusplus
}
#endif
