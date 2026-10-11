// SPDX-License-Identifier: GPL-3.0-or-later
// include/app_motion_activity.h
#pragma once

#include <atomic>
#include <chrono>

namespace helix {

/**
 * Tracks app-initiated motion (jog) RPC activity so the discretionary-gcode
 * busy guard can tell self-inflicted busy (idle_timeout == "Printing" because
 * OUR jog is executing) from an external blocking op (calibration, console
 * gcode from another UI). Thread-safe: sends stamp from the main thread,
 * acks/errors from the websocket thread.
 *
 * Invariant: MoonrakerMotionAPI wraps BOTH the success and error callback of
 * every stamped send, and the request tracker guarantees one of them fires
 * (including on timeout) — so inflight_ cannot leak upward permanently.
 */
class AppMotionActivity {
  public:
    using clock = std::chrono::steady_clock;
    static constexpr std::chrono::seconds GRACE_WINDOW{2};
    /// Longest a busy episode can pass as the app's own after its last send.
    static constexpr std::chrono::seconds MAX_SELF_BUSY{30};

    void note_sent(clock::time_point now = clock::now()) {
        last_sent_ns_.store(now.time_since_epoch().count(), std::memory_order_relaxed);
        inflight_.fetch_add(1, std::memory_order_relaxed);
    }

    void note_done(clock::time_point now = clock::now()) {
        last_done_ns_.store(now.time_since_epoch().count(), std::memory_order_relaxed);
        // Clamp at zero if a done arrives without a matching send.
        int prev = inflight_.fetch_sub(1, std::memory_order_relaxed);
        if (prev <= 0) {
            inflight_.store(0, std::memory_order_relaxed);
        }
    }

    bool recently_active(clock::time_point now = clock::now()) const {
        if (inflight_.load(std::memory_order_relaxed) > 0) {
            return true;
        }
        const auto last_ns = last_done_ns_.load(std::memory_order_relaxed);
        if (last_ns == 0) {
            return false;
        }
        const clock::time_point last{clock::duration{last_ns}};
        return (now - last) < GRACE_WINDOW;
    }

    /**
     * Whether a busy episode that began at @p episode_start is the app's own
     * motion. The ack arrives when Klipper queues a move, so a long absolute
     * move keeps idle_timeout "Printing" well past recently_active().
     *
     * Ownership is decided once per episode, from the send the app made
     * last before first asking about it: the episode is the app's when that
     * send landed within GRACE_WINDOW of its start. A later send never
     * adopts an episode someone else began. It then lapses MAX_SELF_BUSY
     * after the latest send, bounding how long an operation that starts
     * mid-move can pass as ours.
     */
    bool owns_busy_episode(clock::time_point episode_start, clock::time_point now = clock::now()) {
        const long long start_ns = episode_start.time_since_epoch().count();
        const auto last_ns = last_sent_ns_.load(std::memory_order_relaxed);
        if (last_ns == 0) {
            return false;
        }
        const clock::time_point last{clock::duration{last_ns}};
        if (decided_episode_ns_.load(std::memory_order_relaxed) != start_ns) {
            const bool started =
                last >= episode_start - GRACE_WINDOW && last <= episode_start + GRACE_WINDOW;
            episode_owned_.store(started, std::memory_order_relaxed);
            decided_episode_ns_.store(start_ns, std::memory_order_relaxed);
        }
        return episode_owned_.load(std::memory_order_relaxed) && (now - last) < MAX_SELF_BUSY;
    }

  private:
    std::atomic<int> inflight_{0};
    std::atomic<long long> last_sent_ns_{0};
    std::atomic<long long> decided_episode_ns_{0};
    std::atomic<bool> episode_owned_{false};
    std::atomic<long long> last_done_ns_{0};
};

} // namespace helix
