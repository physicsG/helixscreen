// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "print_eta_estimator.h"

#include <algorithm>

namespace helix {

void PrintEtaEstimator::reset() {
    smoothed_remaining_ = 0.0;
    has_smoothed_ = false;
}

int PrintEtaEstimator::remaining_seconds(const PrintEtaInputs& in) {
    const int progress = in.progress_pct;

    // Still preparing: what is left is the rest of the prep plus the whole job.
    if (progress == 0 && in.preprint_remaining_s > 0 && in.slicer_estimate_s > 0) {
        return in.preprint_remaining_s + in.slicer_estimate_s;
    }

    if (progress >= 100) {
        return 0;
    }

    if (progress < 1) {
        return NO_ESTIMATE;
    }

    // Too early to extrapolate: a handful of seconds against a whole job turns
    // any rate estimate into noise, so the slicer's number stands alone.
    if (progress < 5 && in.slicer_estimate_s > 0) {
        return in.slicer_estimate_s * (100 - progress) / 100;
    }

    if (in.print_duration_s > 0) {
        double raw_remaining =
            static_cast<double>(in.print_duration_s) * (100 - progress) / progress;

        // Early on, damp the extrapolation toward the slicer's estimate. The
        // weight falls to zero at BLEND_BELOW_PCT, so the two agree at the
        // handover and the number does not jump as the blend switches off.
        if (progress < BLEND_BELOW_PCT && in.slicer_estimate_s > 0) {
            const double slicer_weight =
                static_cast<double>(BLEND_BELOW_PCT - progress) / BLEND_BELOW_PCT;
            const double slicer_remaining =
                static_cast<double>(in.slicer_estimate_s) * (100.0 - progress) / 100.0;
            raw_remaining =
                slicer_weight * slicer_remaining + (1.0 - slicer_weight) * raw_remaining;
        }

        // Alpha rises with progress, so the estimate is very stable while the
        // sample is small and converges once the rate is worth believing:
        // 0.06 at 1%, 0.20 at 15%, capped at 0.30 from 25% on.
        const double alpha = std::min(0.3, 0.05 + progress * 0.01);
        if (!has_smoothed_) {
            smoothed_remaining_ = raw_remaining;
            has_smoothed_ = true;
        } else {
            smoothed_remaining_ = alpha * raw_remaining + (1.0 - alpha) * smoothed_remaining_;
        }
        return static_cast<int>(smoothed_remaining_);
    }

    // Printing has not started clocking yet, so there is no rate to observe.
    if (in.slicer_estimate_s > 0) {
        return in.slicer_estimate_s * (100 - progress) / 100;
    }

    return NO_ESTIMATE;
}

} // namespace helix
