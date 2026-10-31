// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "sweep_synth.h"

#include <cmath>
#include <limits>
#include <utility>

namespace helix::calibration {

namespace {

constexpr double kTwoPi = 2.0 * 3.14159265358979323846;

/// Uniform in [-1, 1), the same LCG the belt tests' Hiss uses.
float next_hiss(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) / static_cast<float>(1u << 23) - 1.0f;
}

} // namespace

SweepSynth::SweepSynth(SweepSynthSpec spec, SweepAxisGain gain)
    : spec_(std::move(spec)), gain_(std::move(gain)), rng_(spec_.seed) {}

double SweepSynth::time_s() const {
    return static_cast<double>(index_) / static_cast<double>(spec_.rate_hz);
}

float SweepSynth::drive_hz_at(double t_s) const {
    if (t_s < spec_.drive_start_s) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const double f = static_cast<double>(spec_.plan.freq_start_hz) +
                     static_cast<double>(spec_.plan.hz_per_sec) * (t_s - spec_.drive_start_s);
    if (f > static_cast<double>(spec_.plan.freq_end_hz)) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return static_cast<float>(f);
}

std::vector<AccelSample> SweepSynth::next(size_t count) {
    std::vector<AccelSample> out(count);
    const double rate = static_cast<double>(spec_.rate_hz);
    for (auto& sample : out) {
        const double t = time_s();
        const float f = drive_hz_at(t);

        std::array<float, 3> drive{0.0f, 0.0f, 0.0f};
        if (std::isfinite(f)) {
            const auto g = gain_ ? gain_(f) : std::array<float, 3>{0.0f, 0.0f, 0.0f};
            const auto s = static_cast<float>(std::sin(phase_));
            drive = {g[0] * s, g[1] * s, g[2] * s};
            phase_ += kTwoPi * static_cast<double>(f) / rate;
            if (phase_ >= kTwoPi) {
                phase_ -= kTwoPi;
            }
        }

        sample.time = static_cast<float>(t);
        sample.x = spec_.gravity_mm_s2 + drive[0] + spec_.noise_mm_s2 * next_hiss(rng_);
        sample.y = drive[1] + spec_.noise_mm_s2 * next_hiss(rng_);
        sample.z = drive[2] + spec_.noise_mm_s2 * next_hiss(rng_);
        ++index_;
    }
    return out;
}

} // namespace helix::calibration
