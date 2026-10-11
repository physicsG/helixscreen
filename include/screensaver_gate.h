// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "screensaver_cpu_clock.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

/**
 * @file screensaver_gate.h
 * @brief How much CPU a running screensaver may use, and what happens when it uses more
 *
 * Pure: the manager feeds it samples from the process CPU clock and acts on its answers.
 */

namespace helix::ui {

/// Share of one core a running saver may use: 4 or more cores 0.50, 3 cores 0.37, 2 cores 0.25,
/// otherwise 0.10 (a core count that failed to parse reads as one core). Halved while printing.
double saver_budget_share(int cores, bool printing);

enum class GateDecision {
    KEEP,      ///< within budget: the level stays
    STEP_DOWN, ///< over budget: run one level lower
    TOO_HEAVY, ///< over budget at the lowest level: the board cannot afford this saver
};

/// What a window measuring `share` of one core against `budget` means for a saver running at
/// `level` of `level_count` levels. A run never steps up.
GateDecision decide_saver_level(double share, double budget, size_t level, size_t level_count);

/// The app's CPU rate while no saver runs, over the last idle stretch of up to SPAN_NS.
class IdleBaseline {
  public:
    static constexpr uint64_t SPAN_NS = 10000000000ULL;
    /// With fewer seconds of samples than this the rate reads as 0, which can only make the
    /// gate step down early, never late.
    static constexpr uint64_t MIN_SPAN_NS = 3000000000ULL;

    void add(CpuSample sample);
    void reset();
    /// Cores of CPU per core of wall time between the oldest and newest sample held.
    double rate() const;
    size_t size() const {
        return samples_.size();
    }

  private:
    std::deque<CpuSample> samples_;
};

/// Measures one run of a saver: a warm-up, then consecutive windows for as long as it runs.
class SaverGateSession {
  public:
    static constexpr uint64_t WARMUP_NS = 1000000000ULL;
    static constexpr uint64_t WINDOW_NS = 5000000000ULL;

    /// Starts a run at `start`, with `baseline_rate` cores of idle work to subtract.
    void begin(CpuSample start, double baseline_rate);
    /// Starts a new window at `now` without a warm-up, for a level that just changed.
    void restart_window(CpuSample now);
    /// Feeds a sample; returns `(cpu delta - baseline * wall) / wall` of the window that closed
    /// at it, as a share of one core, or nullopt while none has.
    std::optional<double> add(CpuSample sample);

  private:
    CpuSample start_{};
    CpuSample window_start_{};
    bool warming_up_ = true;
    double baseline_rate_ = 0.0;
};

/// HELIX_SCREENSAVER_BUDGET_PCT (a whole percent of one core, 1 to 400) and
/// HELIX_SCREENSAVER_LEVEL (a whole level, 0 to 99); a malformed value warns and reads as unset.
struct SaverEnvOverrides {
    std::optional<uint32_t> budget_pct;
    std::optional<uint32_t> level;
};

SaverEnvOverrides saver_env_overrides();

} // namespace helix::ui
