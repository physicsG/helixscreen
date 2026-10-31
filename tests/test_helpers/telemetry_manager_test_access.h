// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "system/telemetry_manager.h"

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

// Test-only seam. TelemetryManager declares this class as a friend, so a test
// can observe whether try_send() cleared its send-interval gate.
//
// The gate's three inputs are all private: the backoff multiplier says whether
// the previous attempt failed, last_send_time_ says how long ago it was, and
// the pass/skip decision leaves no other trace — a cleared gate stamps
// last_send_time_, a skipped one does not. Without these, the spacing between
// a failed send and its retry is unreachable from a test.
//
// disable_network() is what makes calling try_send() safe: it puts the send
// path into the state a device with no CA bundle is already in, so do_send()
// returns before it builds a request. The gate still runs, which is the part
// under test.
//
// advance_clock(), fire_auto_send_tick() and simulate_unclean_exit() let a
// test walk a boot through hours of snapshots and a power cut. set_in_flight()
// stands in for a send that has claimed the front of the queue.
//
// Kept out of the production header so no shipped code path can suppress a
// send or rewrite the send clock (the "no _for_testing methods in headers"
// lint, L065/L088).
class TelemetryManagerTestAccess {
  public:
    /// The hardware profile's `ams` section for @p hw.
    static nlohmann::json hw_ams_section(const TelemetryManager& t,
                                         const helix::PrinterDiscovery& hw) {
        return t.build_hw_ams_section(hw);
    }

    /// Make do_send() a no-op so try_send() can be called without HTTP. Leaves
    /// the send-interval gate, the thread spawn and the queue untouched.
    static void disable_network(TelemetryManager& t) {
        t.ssl_verified_ = true;
        t.send_disabled_ = true;
    }

    /// Pretend the previous attempt failed @p backoff doublings ago. 1 means
    /// the last send succeeded, which is the healthy 24-hour cadence.
    static void set_backoff(TelemetryManager& t, int backoff) {
        t.backoff_multiplier_.store(backoff);
    }

    static int backoff(const TelemetryManager& t) {
        return t.backoff_multiplier_.load();
    }

    /// Move the send clock so a test can choose how long ago the last attempt
    /// was without waiting.
    static void set_last_send_time(TelemetryManager& t,
                                   std::chrono::steady_clock::time_point when) {
        t.last_send_time_ = when;
        t.send_attempted_ = true;
    }

    /// try_send() stamps this when it clears the interval gate and leaves it
    /// alone when it skips, so it is the observable for "did the gate pass".
    static std::chrono::steady_clock::time_point last_send_time(const TelemetryManager& t) {
        return t.last_send_time_;
    }

    /// Events discarded by enqueue_event() since the last batch was accepted.
    /// The queue size alone cannot distinguish a window that fit from one that
    /// overflowed, because both leave it at kMaxQueueSize.
    static size_t events_dropped_since_send(const TelemetryManager& t) {
        std::lock_guard<std::mutex> lock(t.mutex_);
        return t.events_dropped_since_send_;
    }

    /// Setting changes waiting for the debounce flush, as "setting|old|new".
    static std::vector<std::string> pending_setting_changes(const TelemetryManager& t) {
        std::vector<std::string> out;
        for (const auto& c : t.pending_settings_changes_) {
            out.push_back(c.setting + "|" + c.old_value + "|" + c.new_value);
        }
        return out;
    }

    static void clear_pending_setting_changes(TelemetryManager& t) {
        t.pending_settings_changes_.clear();
    }

    /// Stop the session trackers' clock: from here it moves only by
    /// advance_clock(). It survives init(), as time does across a reboot.
    static void freeze_clock(TelemetryManager& t) {
        t.manual_clock_ = std::chrono::steady_clock::now();
    }

    static void unfreeze_clock(TelemetryManager& t) {
        t.manual_clock_.reset();
    }

    /// Move the session trackers' clock forward, so a test can place a
    /// disconnect at hour 3:50 without waiting for it.
    static void advance_clock(TelemetryManager& t, std::chrono::steady_clock::duration by) {
        if (!t.manual_clock_) {
            freeze_clock(t);
        }
        *t.manual_clock_ += by;
    }

    /// The session trackers' clock, which every delta window is measured on.
    static std::chrono::steady_clock::time_point now(const TelemetryManager& t) {
        return t.tracker_now();
    }

    /// Run the hourly auto-send timer's body once.
    static void fire_auto_send_tick(TelemetryManager& t) {
        t.on_auto_send_tick();
    }

    /// End the boot the way a power cut does: no shutdown(), so nothing is
    /// flushed or deleted, and the next init() starts a new boot.
    static void simulate_unclean_exit(TelemetryManager& t) {
        if (t.send_thread_.joinable()) {
            t.send_thread_.join();
        }
        if (lv_is_initialized()) {
            t.stop_auto_send();
        }
        t.initialized_.store(false);
    }

    /// Mark the first @p count queued events as the batch a send has in flight.
    static void set_in_flight(TelemetryManager& t, size_t count) {
        std::lock_guard<std::mutex> lock(t.mutex_);
        t.in_flight_ = count;
    }

    /// A send that claimed the front batch and ended without a confirmed
    /// removal: a non-2xx, a timeout, or a 2xx that arrived during shutdown.
    static void fail_send(TelemetryManager& t) {
        t.claim_batch();
        t.release_in_flight();
    }

    /// A send that has claimed the front batch and not yet ended.
    static void claim_batch(TelemetryManager& t) {
        t.claim_batch();
    }

    /// Wait for the send thread try_send() spawned, so its end-of-send state
    /// is settled before a test reads the queue.
    static void join_send_thread(TelemetryManager& t) {
        if (t.send_thread_.joinable()) {
            t.send_thread_.join();
        }
    }

    static std::string boot_id(const TelemetryManager& t) {
        return t.boot_id_;
    }
};
