// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "refresh_timing.h"

#include <cstdint>
#include <functional>
#include <lvgl.h>

namespace helix {

class RefreshPeriodHoldTestAccess;

/**
 * @brief Reference-counted hold that runs the display refresh at a configured period
 *
 * A screensaver animates without pause, and LVGL's default 33 ms refresh holds it near
 * 30 fps on a 60 Hz panel. While this hold is out, the default display's refresh timer
 * and the animation timer run at period() instead, and at the running saver's frame period
 * once the saver gives one. The refresh timer pauses itself whenever nothing is
 * invalidated, so a shorter period costs nothing on a still frame.
 *
 * - The first acquire() records both timers' periods and sets them to period(). With no
 *   period configured, no default display, or no refresh timer, it changes nothing.
 * - follow() runs both timers at the saver's frame period from then on, taking them if
 *   acquire() did not. SaverBase gives period() as level 0's frame period when one is
 *   configured, so the saver and the display refresh stay equal at every level.
 * - While it runs the timers, loop_min_sleep_ms() gives the main loop this hold's floor, so a
 *   frame due every period() is not held back by the loop's usual floor.
 * - Nested acquire() calls only count.
 * - The final release() puts back the periods the first acquire() recorded, which is the
 *   global HELIX_REFR_PERIOD_MS when one is configured. It writes only to the display it
 *   changed, and skips that display if it was deleted mid-hold.
 * - release() without a matching acquire() does nothing.
 *
 * Separate from ScreenHideHold: the static software sleep overlay hides the screen too,
 * and must not refresh fast. Defined in display_manager.cpp, which every build compiles;
 * the screensaver sources that take this hold are optional. Main thread only.
 */
class RefreshPeriodHold {
  public:
    /// Period, in ms, the next first acquire() applies. 0 leaves the timers alone.
    void set_period(uint32_t period_ms) {
        m_period_ms = period_ms;
    }

    uint32_t period() const {
        return m_period_ms;
    }

    /// Main-loop floor, in ms, while this hold runs the timers. 0 leaves the loop's own floor.
    void set_loop_min_sleep(uint32_t ms) {
        m_loop_min_sleep_ms = ms;
    }

    /// The shortest sleep the main loop takes: this hold's floor while it runs the refresh
    /// timer, otherwise `base_ms`.
    uint32_t loop_min_sleep_ms(uint32_t base_ms) const {
        return m_display != nullptr && m_loop_min_sleep_ms != 0 ? m_loop_min_sleep_ms : base_ms;
    }

    void acquire();

    /// While held, both timers run at `saver_period_ms`, the running saver's frame period,
    /// until the final release(). Not held, or given 0, it does nothing.
    void follow(uint32_t saver_period_ms);

    void release();

    /**
     * @brief Runs `set_baseline` with the hold's timers put back, then takes them again
     *
     * For a caller that sets new global periods while a screensaver may be running. Not
     * held, it only runs `set_baseline`. Held, the periods `set_baseline` leaves become the
     * ones the final release() puts back, and period() is applied again on whichever
     * display is default by then.
     */
    void rebase(const std::function<void()>& set_baseline);

    /// True between the first acquire() and the final release().
    bool is_held() const {
        return m_count > 0;
    }

  private:
    friend class RefreshPeriodHoldTestAccess;

    /// Records both timers' periods and sets them to period(), as the first acquire() does.
    void take_timers();

    /// Puts back the periods take_timers() changed, if it changed any, and forgets them.
    void restore_timers();

    lv_display_t* m_display = nullptr; ///< Display whose refresh timer acquire() changed
    uint32_t m_saved_refr_period_ms = 0;
    uint32_t m_saved_anim_period_ms = 0;
    bool m_saved_anim = false;
    uint32_t m_period_ms = 0;
    uint32_t m_saver_period_ms = 0; ///< the running saver's frame period while held, or 0

    uint32_t effective_period() const {
        return m_saver_period_ms != 0 ? m_saver_period_ms : m_period_ms;
    }
    uint32_t m_loop_min_sleep_ms = 0;
    int m_count = 0;
};

/// The hold every running screensaver shares.
RefreshPeriodHold& active_refresh_period_hold();

/**
 * @brief Applies refresh pacing to the running LVGL instance
 *
 * Sets the screensaver hold's period. When timing.refr_period_ms is set, also sets the
 * default display's refresh timer and the animation timer to it and, with scope_all,
 * every input device's read timer and the UpdateQueue drain timer. While a screensaver holds
 * the refresh period, the display and animation timers keep the hold's period and the global
 * one becomes what its final release puts back. Setting a period never resumes a paused
 * timer. Main thread only.
 */
void apply_refresh_timing(const RefreshTiming& timing);

} // namespace helix
