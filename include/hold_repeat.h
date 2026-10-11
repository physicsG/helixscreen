// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace helix {

/** Pure timing decisions for hold-to-repeat input.
 *
 * Nothing here touches LVGL, so tests drive it with plain elapsed-millisecond
 * values instead of a running periodic timer (the unit-test harness only
 * executes one-shot timers). A press arms the machine; the owner then asks
 * on_elapsed() at whatever cadence it polls. No repeat fires before DELAY_MS,
 * then one fires per INTERVAL_MS. After release, swallow_click() says whether
 * the CLICKED that follows must be dropped: a hold that repeated must not add
 * one extra jog on release, while a plain tap with no repeat still jogs
 * exactly once via the existing click path.
 */
class HoldRepeat {
  public:
    /// Held this long before the first repeat: an ordinary tap never repeats.
    static constexpr uint32_t DELAY_MS = 400;
    /// One repeat every this long once the delay has passed.
    static constexpr uint32_t INTERVAL_MS = 150;

    void press() {
        active_ = true;
        fired_ = false;
        next_ms_ = DELAY_MS;
    }

    /// Finger up: ticks must stop, but the swallow answer survives for the
    /// CLICKED that follows.
    void release() {
        active_ = false;
    }

    /// Forget the press entirely (press lost, teardown).
    void cancel() {
        active_ = false;
        fired_ = false;
    }

    /// Whether this many ms since the press fires a repeat now.
    bool on_elapsed(uint32_t ms_since_press) {
        if (!active_ || ms_since_press < next_ms_) {
            return false;
        }
        fired_ = true;
        // A late poll (stalled main loop) fires once and resumes on the grid
        // at the first boundary after now; repaying every missed interval
        // would burst the head forward one repeat per poll.
        const uint32_t phase = (ms_since_press - DELAY_MS) % INTERVAL_MS;
        next_ms_ = ms_since_press + (INTERVAL_MS - phase);
        return true;
    }

    bool active() const {
        return active_;
    }

    /// Drop the CLICKED that follows a release: true only when at least one
    /// repeat fired for this press.
    bool swallow_click() const {
        return fired_;
    }

  private:
    bool active_ = false;
    bool fired_ = false;
    uint32_t next_ms_ = DELAY_MS;
};

} // namespace helix
