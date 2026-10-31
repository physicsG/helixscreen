// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

/**
 * @file gcode_preview_cost.h
 * @brief Whether a streamed G-code preview is worth its CPU on this board.
 *
 * A streamed 2D preview costs one full scan of the file (the layer index) and a
 * ghost pass of comparable size. On a printer's own board that CPU comes out of
 * the same two cores Klipper runs on, so Auto shows the slicer thumbnail instead
 * when the projected cost exceeds a budget, tighter while a print runs.
 *
 * Cost is projected from how fast this board scans an index (MB of G-code per
 * CPU second): learned from earlier idle scans before any work starts, or
 * measured live from the first slice of this file's scan when nothing was
 * learned yet. Pure functions; the callers read the inputs.
 */

namespace helix::gcode {

/// Projected CPU seconds a preview may cost with the printer idle.
inline constexpr double kPreviewBudgetIdleS = 15.0;

/// Projected CPU seconds a preview may cost while a print runs.
inline constexpr double kPreviewBudgetPrintingS = 5.0;

/// The ghost pass's CPU over the index scan's, for the same file. Measured on a
/// CC1 between 0.5 and 1.05 depending on the file and sampling density; the top
/// of the range, so a projection never undercounts.
inline constexpr double kGhostCostOverIndex = 1.0;

/// A live projection needs this share of the file scanned and this much CPU
/// spent before it may stop a scan: earlier, start-up noise dominates.
inline constexpr double kProbeMinFraction = 0.02;
inline constexpr double kProbeMinCpuS = 0.25;

enum class PreviewVerdict {
    Affordable,   ///< Within budget: build the preview
    TooExpensive, ///< Over budget: show the thumbnail
    Unknown,      ///< Nothing to project from yet
};

constexpr double preview_budget_s(bool printing) {
    return printing ? kPreviewBudgetPrintingS : kPreviewBudgetIdleS;
}

/// CPU seconds a streamed preview of @p bytes costs at @p index_mb_per_cpu_s,
/// index scan and ghost pass together. 0 when the rate is unknown (<= 0).
constexpr double projected_preview_cpu_s(uint64_t bytes, double index_mb_per_cpu_s) {
    if (!(index_mb_per_cpu_s > 0.0)) {
        return 0.0;
    }
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    return mb / index_mb_per_cpu_s * (1.0 + kGhostCostOverIndex);
}

/// Before any work: is a file of @p bytes affordable at a learned rate?
constexpr PreviewVerdict judge_preview(uint64_t bytes, double index_mb_per_cpu_s, bool printing) {
    if (!(index_mb_per_cpu_s > 0.0)) {
        return PreviewVerdict::Unknown;
    }
    return projected_preview_cpu_s(bytes, index_mb_per_cpu_s) > preview_budget_s(printing)
               ? PreviewVerdict::TooExpensive
               : PreviewVerdict::Affordable;
}

/// During the index scan: @p fraction of the file has cost @p cpu_s so far.
/// Unknown until the sample is large enough to trust (kProbeMinFraction and
/// kProbeMinCpuS), then the projection for the whole preview against budget.
constexpr PreviewVerdict judge_index_progress(double fraction, double cpu_s, bool printing) {
    if (!(fraction >= kProbeMinFraction) || !(cpu_s >= kProbeMinCpuS)) {
        return PreviewVerdict::Unknown;
    }
    const double projected = cpu_s / fraction * (1.0 + kGhostCostOverIndex);
    return projected > preview_budget_s(printing) ? PreviewVerdict::TooExpensive
                                                  : PreviewVerdict::Affordable;
}

} // namespace helix::gcode
