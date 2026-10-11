// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file print_eta_estimator.h
 * @brief How long the running print has left.
 *
 * Pure logic: no LVGL, no Config, no threads, same shape as PreprintPredictor.
 * The rule is worth having on its own because it is the only place that blends
 * the slicer's estimate with the rate actually observed, and because a number
 * the user watches for hours deserves to be assertable without standing up a
 * panel to read it off.
 */

namespace helix {

/// One status payload's worth of input to the ladder.
struct PrintEtaInputs {
    /// 0-100. Below 1 the extrapolation has no sample to work from.
    int progress_pct = 0;
    /// Seconds actually spent printing. Deliberately NOT total_duration, which
    /// counts preheat and pauses and so inflates every extrapolation from it.
    int print_duration_s = 0;
    /// The slicer's estimate for the whole job, from metadata or the header.
    int slicer_estimate_s = 0;
    /// Predicted pre-print time still to run, while the job is still preparing.
    int preprint_remaining_s = 0;
};

/**
 * @brief Remaining print time from progress, elapsed time and the slicer estimate.
 *
 * A class rather than a free function because the answer is smoothed, and
 * smoothing has to remember the previous answer. Extrapolating from a small
 * sample is noisy enough that the raw number visibly jitters, so the estimate
 * is damped hard early and converges as the sample grows.
 */
class PrintEtaEstimator {
  public:
    /// These inputs support no answer. The caller leaves its subject alone
    /// rather than publishing a zero, which would read as "finished".
    static constexpr int NO_ESTIMATE = -1;

    /// Seconds remaining, or NO_ESTIMATE. Advances the smoothing state.
    [[nodiscard]] int remaining_seconds(const PrintEtaInputs& in);

    /// Forget the smoothed value. A new print's first sample must not be
    /// averaged against the previous print's last one.
    void reset();

  private:
    /// Below this progress the observed rate is blended with the slicer's
    /// estimate, weighted toward the slicer as progress approaches zero.
    static constexpr int BLEND_BELOW_PCT = 15;

    double smoothed_remaining_ = 0.0;
    bool has_smoothed_ = false;
};

} // namespace helix
