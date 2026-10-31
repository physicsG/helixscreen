// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace helix {

/**
 * @brief Hides the touch that wakes the display from LVGL until the finger lifts
 *
 * The touch that wakes a sleeping, dimmed or screensaver-covered display is only a
 * request to see the screen. Once a press wakes it, every read of that touch is
 * reported as released, at the point the pointer last rested, so LVGL never sees a
 * press, a drag or a long press it could act on.
 *
 * The latch ends on a raw release, unless that release comes inside kRearmMs of the
 * waking press. A finger landing on a resistive or noisy panel chatters (press,
 * release, press), and a panel just powered back on can report a stray contact, so a
 * release that early keeps the latch armed and a re-press inside the window is
 * swallowed too. It ends at the first released read at or past the window, or at a
 * press that arrives after the window with the finger lifted; that press is an
 * ordinary touch.
 *
 * One latch per pointer device: it tracks a single finger's down/up sequence. Pure, and
 * fed from that device's read callback on the main thread.
 */
class WakeTouchLatch {
  public:
    struct Sample {
        bool pressed = false;
        int32_t x = 0;
        int32_t y = 0;
    };

    /// The chatter window after the waking press. A deliberate second tap takes longer
    /// than this to land, and a contact bounce or a panel's power-on glitch settles
    /// within it.
    static constexpr uint32_t kRearmMs = 200;

    /**
     * @brief Rewrite one raw read taken at @p now_ms
     *
     * @param press_wakes Called as `bool(x, y)` for a press that is not already being
     *        swallowed; true means this press is waking the display.
     * @return The sample to hand LVGL.
     */
    template <typename PressWakes>
    Sample filter(Sample raw, uint32_t now_ms, PressWakes&& press_wakes) {
        if (m_swallowing) {
            const bool in_window = now_ms - m_wake_ms < kRearmMs;
            if (raw.pressed && (in_window || !m_lifted)) {
                m_lifted = false; // a chatter re-press held past the window stays swallowed
                return {false, m_rest.x, m_rest.y};
            }
            if (!raw.pressed && in_window) {
                m_lifted = true;
                return {false, m_rest.x, m_rest.y};
            }
            m_swallowing = false;
        }
        if (raw.pressed && !m_swallowing && press_wakes(raw.x, raw.y)) {
            m_swallowing = true;
            m_lifted = false;
            m_wake_ms = now_ms;
            return {false, m_rest.x, m_rest.y};
        }
        if (!raw.pressed) {
            m_rest = raw;
        }
        return raw;
    }

    bool swallowing() const {
        return m_swallowing;
    }

  private:
    bool m_swallowing = false;
    bool m_lifted = false; // a raw release has been seen since the waking press
    uint32_t m_wake_ms = 0;
    Sample m_rest{};
};

} // namespace helix
