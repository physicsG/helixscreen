// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef HELIX_ENABLE_SCREENSAVER

#include "screensaver_motion.h"

#include <cstdint>
#include <functional>
#include <lvgl.h>

namespace helix::ui {

/**
 * @brief A screensaver's frame timer
 *
 * Calls back with the time since the previous call, read from lv_tick_get() through a
 * MotionClock, so motion stays a function of elapsed time whatever the period.
 */
class SaverFrameTimer {
  public:
    using FrameFn = std::function<void(uint32_t dt_ms)>;

    SaverFrameTimer() = default;
    SaverFrameTimer(const SaverFrameTimer&) = delete;
    SaverFrameTimer& operator=(const SaverFrameTimer&) = delete;

    /// Cancels the timer, so an owner destroyed while running never leaves it armed on freed
    /// memory.
    ~SaverFrameTimer();

    /// Starts calling `on_frame` every `period_ms`. A running timer is cancelled first.
    void start(uint32_t period_ms, FrameFn on_frame);

    /// Changes the period of a running timer without resetting the clock, so the next call
    /// still gets the whole time since the previous one.
    void set_period(uint32_t period_ms);

    /// Stops the callbacks. Safe from inside lv_timer_handler and after lv_deinit().
    void cancel();

    lv_timer_t* timer() const {
        return timer_;
    }

  private:
    static void timer_cb(lv_timer_t* timer);

    lv_timer_t* timer_ = nullptr;
    screensaver::MotionClock clock_;
    FrameFn on_frame_;
};

} // namespace helix::ui

#endif // HELIX_ENABLE_SCREENSAVER
