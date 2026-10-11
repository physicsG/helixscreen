// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "filament_sensor_manager.h"

#include <chrono>
#include <mutex>

namespace helix {

// Friend of FilamentSensorManager: returns the singleton to a clean, synchronous
// state with the startup grace period already expired.
class FilamentSensorManagerTestAccess {
  public:
    static void reset(FilamentSensorManager& mgr) {
        std::lock_guard<std::recursive_mutex> lock(mgr.mutex_);

        // Clear all sensors and states
        mgr.sensors_.clear();

        // Reset master enabled
        mgr.master_enabled_ = true;

        // Clear callback
        mgr.state_change_callback_ = nullptr;

        // Enable sync mode for testing (avoids lv_async_call)
        mgr.sync_mode_ = true;

        // Reset initial status tracking (ensures first update_from_status triggers subjects)
        mgr.initial_status_received_ = false;

        // Reset startup time to 10 seconds in the past so the stabilization grace
        // period is already expired in tests (avoids flaky timing-dependent failures)
        mgr.startup_time_ = std::chrono::steady_clock::now() - std::chrono::seconds(10);

        // Reset subjects if initialized
        if (mgr.subjects_initialized_) {
            lv_subject_set_int(&mgr.runout_detected_, -1);
            lv_subject_set_int(&mgr.toolhead_detected_, -1);
            lv_subject_set_int(&mgr.entry_detected_, -1);
            lv_subject_set_int(&mgr.probe_triggered_, -1);
            lv_subject_set_int(&mgr.any_runout_, 0);
            lv_subject_set_int(&mgr.motion_active_, 0);
            lv_subject_set_int(&mgr.master_enabled_subject_, 1);
            lv_subject_set_int(&mgr.sensor_count_, 0);
        }
    }

    static void clear_startup_grace_period(FilamentSensorManager& mgr) {
        mgr.startup_time_ = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    }
};

} // namespace helix
