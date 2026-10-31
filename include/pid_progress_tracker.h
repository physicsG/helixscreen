// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "thermal_rate_model.h"

#include <cstdint>
#include <optional>

/**
 * @brief Phase-aware PID calibration progress tracker
 *
 * Monitors temperature during PID_CALIBRATE to detect heating vs oscillation
 * phases, count oscillation cycles, and estimate time remaining. Pure C++
 * logic — no LVGL dependency. Fed temperature updates from an observer.
 *
 * Klipper hardcodes TUNE_PID_CYCLES = 5 oscillation cycles.
 */
class PidProgressTracker {
  public:
    enum class Heater { EXTRUDER, BED };
    enum class Phase { IDLE, HEATING, OSCILLATING, COMPLETE };

    /// Start tracking a new calibration
    void start(Heater heater, int target_temp, float current_temp);

    /// Feed a temperature reading. tick_ms = lv_tick_get() or equivalent.
    void on_temperature(float temp, uint32_t tick_ms);

    /// Call when Klipper reports calibration complete
    void mark_complete();

    /// Current phase
    Phase phase() const {
        return phase_;
    }

    /// Progress percentage 0-100
    int progress_percent() const;

    /// Estimated seconds remaining, or nullopt if insufficient data
    std::optional<int> eta_seconds() const;

    /// Number of completed oscillation cycles (downward zero-crossings)
    int oscillation_count() const {
        return oscillation_count_;
    }

    /// Whether a live heating rate measurement has been established
    bool has_measured_heat_rate() const {
        return thermal_model_.measured_rate().has_value();
    }

    /// Measured heating rate in seconds per degree (0 if not yet measured)
    float measured_heat_rate() const {
        return thermal_model_.measured_rate().value_or(0.0f);
    }

    /// Access the underlying thermal rate model
    const ThermalRateModel& thermal_model() const {
        return thermal_model_;
    }

    /// Measured total oscillation duration in seconds (0 if not complete)
    float measured_oscillation_duration() const;

    /// Set historical estimates from previous calibrations
    void set_history(float heat_rate_s_per_deg, float oscillation_duration_s);

    /// Handle a firmware sample-progress callback (sample N of an estimated total)
    void on_sample_progress(int sample, int estimated_total);

    /// Last temperature reading
    float last_temp() const {
        return last_temp_;
    }

    /// Current sample number from the firmware progress callback
    int sample_number() const {
        return sample_number_;
    }

    static constexpr int kExpectedCycles = 5;

    // Smart defaults (used when no history exists)
    static constexpr float kDefaultExtruderHeatRate = 0.5f;      // s/°C
    static constexpr float kDefaultBedHeatRate = 1.5f;           // s/°C
    static constexpr float kDefaultExtruderOscillation = 120.0f; // seconds
    static constexpr float kDefaultBedOscillation = 360.0f;      // seconds

  private:
    Phase phase_ = Phase::IDLE;
    Heater heater_ = Heater::EXTRUDER;
    int target_temp_ = 0;

    // Heating rate measurement (delegated to ThermalRateModel)
    ThermalRateModel thermal_model_;
    float start_temp_ = 0;
    uint32_t start_tick_ = 0;
    float last_temp_ = 0;
    uint32_t last_tick_ = 0;
    bool was_above_target_ = false; // for downward crossing detection

    // Oscillation measurement
    int oscillation_count_ = 0;
    uint32_t first_oscillation_tick_ = 0;
    uint32_t last_oscillation_tick_ = 0;
    float measured_cycle_period_ = 0; // seconds per cycle
    bool has_measured_cycle_ = false;

    // Historical estimates (from previous calibrations)
    float hist_oscillation_duration_ = 0;
    bool has_history_ = false; // for oscillation history only

    // Firmware-reported sample progress
    bool has_sample_progress_ = false;
    int sample_number_ = 0;
    int sample_total_ = 0;

    // Default estimates for the selected heater
    float default_heat_rate() const;
    float default_oscillation_duration() const;

    // Best available estimates
    float best_heat_rate() const;
    float best_oscillation_duration() const;

    // Fraction of total time spent heating (0.05-0.80, time-proportional)
    float heating_fraction() const;
};
