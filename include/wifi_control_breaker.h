// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace helix::wifi {

/**
 * @brief Pure circuit breaker over the WiFi control interface's response time.
 *
 * A wedged WiFi driver leaves the control daemon unable to answer: every
 * wpa_supplicant request then waits out wpa_ctrl's 10 s reply deadline, plus
 * up to 5 s of send retries. Polling such a daemon only queues more of those
 * waits. This decides when to stop asking and when to try again.
 *
 * "Unresponsive" is measured, not reported: a health call (a scan trigger or
 * a status read, each a single short request on a healthy daemon) that takes
 * at least `slow_call_ms` is a strike, whatever it returned, since a
 * timed-out request and a wedged one look identical from here. `trip_after`
 * consecutive strikes open the breaker. While open, allow_call() lets one
 * probe through per backoff window, starting at `first_probe_ms` and doubling
 * to `max_probe_ms`; any health call that answers in time closes it.
 *
 * Owns no timer and no clock: callers pass monotonic milliseconds, so the
 * probes ride whatever polls already exist (the link monitor's, a scan
 * timer's) and the policy is testable without sleeping. Not thread-safe;
 * WiFiManager guards it.
 */
class ControlBreaker {
  public:
    struct Config {
        /// A health call this slow counts as a strike. Healthy replies take
        /// milliseconds; half of wpa_ctrl's reply deadline is already
        /// pathological, and a busy scan on a slow chipset stays well under it.
        uint32_t slow_call_ms = 5000;
        /// Consecutive strikes before opening. Three rides out a single busy
        /// reply and still trips within about 30 s of a wedge.
        int trip_after = 3;
        /// First probe after opening, then doubled per failed probe.
        uint32_t first_probe_ms = 15000;
        uint32_t max_probe_ms = 120000;
    };

    enum class Change { None, Tripped, Recovered };

    ControlBreaker() = default;
    explicit ControlBreaker(const Config& config) : config_(config) {}

    /// Record a finished health call that took `elapsed_ms`, at `now_ms`.
    Change record(uint32_t elapsed_ms, uint64_t now_ms);

    /// Whether a health call may go to the backend at `now_ms`. Always true
    /// while closed. While open, true at most once per backoff window: the
    /// call it admits is the probe, and no other is admitted until record()
    /// hears how it went.
    bool allow_call(uint64_t now_ms);

    /// The admitted probe will never run (its work was dropped): admit the
    /// next one without waiting for a record() that cannot come.
    void abandon_probe() {
        probe_inflight_ = false;
    }

    bool open() const {
        return open_;
    }

    const Config& config() const {
        return config_;
    }

  private:
    Config config_;
    int strikes_ = 0;
    bool open_ = false;
    bool probe_inflight_ = false;
    uint32_t backoff_ms_ = 0;
    uint64_t next_probe_ms_ = 0;
};

} // namespace helix::wifi
