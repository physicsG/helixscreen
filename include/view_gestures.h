// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl.h"

#include <optional>

namespace helix::ui {

/// One-finger drag rotation for the 3D views (G-code preview, bed mesh).
inline constexpr float kRotateDegreesPerPixel = 0.5f;

/// One frame of an LVGL two-finger gesture, in plain types.
struct TwoFingerSample {
    enum class Kind { Pinch, Pan } kind;
    enum class Phase { Ongoing, Recognized, Ended } phase;
    float delta_x; ///< Cumulative centre translation since the gesture began, px
    float delta_y;
    float scale; ///< Cumulative spread, 1.0 = unchanged. Ignored for Pan.
    int start_x; ///< Two-finger centre when the gesture began, screen px
    int start_y;
};

/// Per-widget running totals for the gesture in progress.
struct TwoFingerState {
    float last_dx = 0.0f;
    float last_dy = 0.0f;
    float last_scale = 1.0f;
    bool recognized = false; ///< A Recognized frame has been applied since the last reset
};

/// What a view applies this frame: pan first, then zoom about the anchor.
struct TwoFingerStep {
    float pan_dx = 0.0f; ///< Screen px to move the content
    float pan_dy = 0.0f;
    float zoom = 1.0f; ///< Multiplicative zoom this frame
    int anchor_x = 0;  ///< Live two-finger centre, screen px
    int anchor_y = 0;
    bool active = false; ///< Two fingers are down
    bool ended = false;  ///< The gesture finished this frame
};

/// Turn LVGL's cumulative totals into this frame's pan and zoom. Pure.
TwoFingerStep two_finger_step(const TwoFingerSample& s, TwoFingerState& st);

#if LV_USE_GESTURE_RECOGNITION
/// Read the pinch or two-finger swipe carried by an LV_EVENT_GESTURE.
/// nullopt for any other gesture (including one-finger swipes).
std::optional<TwoFingerSample> read_two_finger_sample(lv_event_t* e);
#endif

} // namespace helix::ui
