// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "bed_occupancy_state.h"

namespace helix {

/// Sets BedOccupancyState's verdict directly, so a plate test can put a part of a chosen
/// height on the bed without staging a print history that produces it.
class BedOccupancyStateTestAccess {
  public:
    static void set_verdict(BedOccupancyState& state, const BedOccupancy& verdict) {
        state.current_ = verdict;
        if (state.subjects_initialized_) {
            lv_subject_set_int(&state.occupied_, verdict.occupied ? 1 : 0);
        }
        state.bump_version();
    }

    /// Runs the history retry timer's callback once, as its period elapsing would. The test
    /// harness's timer pump skips repeating timers. False when no retry is armed.
    static bool fire_history_retry(BedOccupancyState& state) {
        lv_timer_t* timer = state.history_retry_timer_.get();
        if (!timer) {
            return false;
        }
        BedOccupancyState::history_retry_cb(timer);
        return true;
    }

    [[nodiscard]] static bool history_retry_armed(const BedOccupancyState& state) {
        return static_cast<bool>(state.history_retry_timer_);
    }
};

} // namespace helix
