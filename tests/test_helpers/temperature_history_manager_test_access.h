// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "temperature_history_manager.h"

#include <cstdint>
#include <mutex>
#include <string>

// Friend of TemperatureHistoryManager: feeds samples without a Moonraker stream.
class TemperatureHistoryManagerTestAccess {
  public:
    static bool add_sample(TemperatureHistoryManager& m, const std::string& heater_name,
                           int temp_deci, int target_deci, int64_t timestamp_ms) {
        bool stored;
        {
            std::lock_guard<std::mutex> lock(m.mutex_);
            stored = m.add_sample_internal(heater_name, temp_deci, target_deci, timestamp_ms);
        }
        if (stored) {
            m.notify_observers(heater_name);
        }
        return stored;
    }

    /// The log-volume policy add_sample_internal() routes its range verdict
    /// through (#1348).
    static const helix::InvalidSampleTracker& reject_log(const TemperatureHistoryManager& m) {
        return m.reject_log_;
    }
};
