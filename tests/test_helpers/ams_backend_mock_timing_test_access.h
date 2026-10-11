// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ams_backend_mock.h"

/**
 * @file ams_backend_mock_timing_test_access.h
 * @brief Reach the mock's two simulated-time conversions directly.
 *
 * Both are private, and the realistic-mode tests only reach them by sleeping
 * against the delays they produce — which proves a delay is short, not which
 * direction the speed factor was applied in. These expose them as values.
 */
namespace helix {

class AmsBackendMockTimingTestAccess {
  public:
    /// Real milliseconds the mock would wait for a `base_ms` simulated delay.
    static int effective_delay_ms(const AmsBackendMock& b, int base_ms, float variance = 0.0f) {
        return b.get_effective_delay_ms(base_ms, variance);
    }

    /// Simulated drying seconds per real second, dryer multiplier over the flag.
    static int dryer_speed_x(const AmsBackendMock& b) {
        return b.effective_dryer_speed_x();
    }

    /// The dryer's own multiplier, before the global flag composes over it.
    static void set_dryer_speed_x(AmsBackendMock& b, int speed_x) {
        b.dryer_speed_x_ = speed_x;
    }

    /// Mark the backend started without launching its simulation threads, for
    /// tests that only need an operation's precondition guards to pass.
    static void force_started(AmsBackendMock& b) {
        b.running_ = true;
    }

    /// Re-run the lane-observation publish, so a test can force a slot status
    /// and observe what the next simulated frame files for it.
    static void publish_lane_observations(AmsBackendMock& b) {
        b.publish_lane_observations();
    }

    /// The mock persona ships with filament loaded (the interesting default for
    /// UI runs); tests asserting the empty-toolhead guards need the other state.
    static void force_filament_loaded(AmsBackendMock& b, bool loaded) {
        std::lock_guard<std::mutex> lock(b.mutex_);
        b.system_info_.filament_loaded = loaded;
    }
};

} // namespace helix
