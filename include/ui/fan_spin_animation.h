// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file fan_spin_animation.h
 * @brief Shared fan icon spin animation utilities
 *
 * Provides continuous rotation animation for fan icons, scaled proportionally
 * to fan speed. Used by FanDial, FanStackWidget, and FanControlOverlay.
 *
 * @threading Main thread only (LVGL animation API)
 */

#include "lvgl.h"

#include <cstdint>

namespace helix::ui {

/// Minimum spin duration at 100% fan speed (ms per full rotation)
inline constexpr uint32_t kFanSpinMinDurationMs = 600;

/// Maximum spin duration at ~1% fan speed (slow crawl)
inline constexpr uint32_t kFanSpinMaxDurationMs = 6000;

/**
 * @brief LVGL animation exec callback for rotation transform
 *
 * Sets transform_rotation on the target object. Value is in 0.1-degree units.
 */
void fan_spin_anim_cb(void* var, int32_t value);

/**
 * @brief Start continuous spin animation on a fan icon
 *
 * The rotation speed scales inversely with speed_pct: 100% is fastest, 1% is slowest.
 * Replaces any existing spin animation on the icon.
 *
 * @param icon LVGL object to animate (must have transform pivot set)
 * @param speed_pct Fan speed percentage (1-100). Values <= 0 are ignored.
 */
void fan_spin_start(lv_obj_t* icon, int speed_pct);

/**
 * @brief Stop spin animation and reset rotation to 0
 * @param icon LVGL object to stop animating
 */
void fan_spin_stop(lv_obj_t* icon);

} // namespace helix::ui
