// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

/**
 * Register responsive constants for switch sizing based on screen dimensions
 * Must be called AFTER globals.xml is registered and BEFORE any XML that uses switches
 */
void ui_switch_register_responsive_constants(void);

/**
 * Recompute the switch size presets (tiny/small/medium/large) for a display
 *
 * Called at startup from ui_switch_register() and again from
 * theme_manager_refresh_layout_constants() so the presets follow a breakpoint
 * that changes at runtime (#1210). Pass NULL for the default display.
 */
void ui_switch_init_size_presets(lv_display_t* display);

/**
 * Register the ui_switch component with the LVGL XML system
 * Must be called before any XML files using <ui_switch> are registered
 */
void ui_switch_register(void);

/**
 * Create a switch with full HelixScreen theme styling, callable from C++.
 *
 * Equivalent to instantiating `<ui_switch size="..."/>` from XML: applies
 * theme colors (CHECKED indicator, knob, track, disabled state), the size
 * preset (track dimensions, knob padding, margins), and the value-changed
 * sound callback. Use when building rows dynamically in C++ where the XML
 * parser path isn't available.
 *
 * @param parent Parent widget
 * @param size_str One of "tiny", "small", "medium", "large" (NULL → "small")
 * @return Created switch widget, or NULL on failure
 */
lv_obj_t* ui_switch_create_themed(lv_obj_t* parent, const char* size_str);

#ifdef __cplusplus
}
#endif
