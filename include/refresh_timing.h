// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstdint>

namespace helix {

/**
 * @brief Refresh pacing settings
 *
 * A default-constructed value holds the shipped defaults: LVGL's own refresh period, a 5 ms
 * main-loop floor, and a 16 ms period with a 1 ms floor while a screensaver runs. Read from
 * the environment by refresh_timing_from_env() (refresh_timing_env.h).
 */
struct RefreshTiming {
    static constexpr uint32_t kDefaultLoopMinSleepMs = 5;
    static constexpr uint32_t kDefaultScreensaverRefrPeriodMs = 16;
    static constexpr uint32_t kDefaultScreensaverLoopMinSleepMs = 1;
    static constexpr uint32_t kMinPeriodMs = 8;
    static constexpr uint32_t kMaxPeriodMs = 100;
    static constexpr uint32_t kMinLoopSleepMs = 1;
    static constexpr uint32_t kMaxLoopSleepMs = 33;

    /// HELIX_REFR_PERIOD_MS. 0 leaves LVGL's refresh and animation periods alone.
    uint32_t refr_period_ms = 0;
    /// HELIX_REFR_PERIOD_SCOPE=all. Also paces input reads and the update queue.
    bool scope_all = false;
    /// HELIX_SCREENSAVER_REFR_PERIOD_MS. 0 runs screensavers at the global period.
    uint32_t screensaver_refr_period_ms = kDefaultScreensaverRefrPeriodMs;
    /// HELIX_LOOP_MIN_SLEEP_MS. The main loop never sleeps less than this.
    uint32_t loop_min_sleep_ms = kDefaultLoopMinSleepMs;
    /// The main loop's floor while a screensaver holds the refresh period. A set
    /// HELIX_LOOP_MIN_SLEEP_MS sets this too.
    uint32_t screensaver_loop_min_sleep_ms = kDefaultScreensaverLoopMinSleepMs;
};

/**
 * @brief How long the main loop sleeps before its next lv_timer_handler() call
 *
 * LVGL's hint for when the next timer is due, capped so the loop stays responsive (33 ms
 * awake, 200 ms while the display sleeps and only wake events matter) and floored so a
 * timer that is already due cannot spin the loop. A refresh period longer than the awake
 * cap raises the cap to match, so the loop does not wake between refreshes for nothing.
 */
constexpr uint32_t main_loop_sleep_ms(uint32_t time_till_next, bool display_sleeping,
                                      const RefreshTiming& timing) {
    constexpr uint32_t kAwakeCapMs = 33;
    constexpr uint32_t kSleepingCapMs = 200;
    const uint32_t cap =
        display_sleeping ? kSleepingCapMs : std::max(kAwakeCapMs, timing.refr_period_ms);
    return std::max(std::min(time_till_next, cap), timing.loop_min_sleep_ms);
}

} // namespace helix
