// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "belt_tension_types.h"
#include "sweep_response_estimator.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

/**
 * @file sweep_synth.h
 * @brief A synthetic accelerometer stream of one resonance sweep
 *
 * Feeds the mock printer's live preview and the estimator's tests from one
 * generator, so both see the same ramp the estimator assumes. Pure and
 * deterministic: the hiss comes from a fixed-seed LCG.
 */

namespace helix::calibration {

struct SweepSynthSpec {
    SweepPlan plan;
    /// Stream time the ramp leaves plan.freq_start_hz. Before it, and after the
    /// ramp passes plan.freq_end_hz, the stream carries hiss and gravity only.
    double drive_start_s = 1.5;
    float rate_hz = 3053.0f;
    /// Uniform hiss in [-noise, noise) on every axis.
    float noise_mm_s2 = 30.0f;
    /// Constant on X, where the real captures carry gravity.
    float gravity_mm_s2 = 9810.0f;
    uint32_t seed = 2468u;
};

/// Drive amplitude in mm/s^2 on x, y and z at a drive frequency.
using SweepAxisGain = std::function<std::array<float, 3>(float drive_hz)>;

class SweepSynth {
  public:
    SweepSynth(SweepSynthSpec spec, SweepAxisGain gain);

    /// The next `count` samples, continuing where the previous call stopped.
    /// The drive phase is the integral of the ramp, so it is continuous across calls.
    [[nodiscard]] std::vector<AccelSample> next(size_t count);

    /// Stream time of the next sample.
    [[nodiscard]] double time_s() const;
    /// The ramp's frequency at `t_s`; NaN outside it.
    [[nodiscard]] float drive_hz_at(double t_s) const;

  private:
    SweepSynthSpec spec_;
    SweepAxisGain gain_;
    uint64_t index_ = 0;
    double phase_ = 0.0;
    uint32_t rng_;
};

} // namespace helix::calibration
