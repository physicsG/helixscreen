// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file gcode_ghost_sampling.h
 * @brief How many layers the background ghost pass may visit, and at what stride.
 *
 * The ghost is the faded silhouette of the layers above the one on screen. It is
 * built once, on a background thread, by walking the file layer by layer. What a
 * single step of that walk costs depends entirely on which parse path the file
 * took:
 *
 * - **Full load** — the step is `&gcode_->layers[i].segments`, a pointer into a
 *   ParsedGCodeFile that is already resident. Walking every layer of even a huge
 *   file is a memory traversal.
 * - **Streaming** — the step is `get_layer_segments(i)`, a seek and parse against
 *   the file on disk. Walking every layer re-parses the entire file, a second
 *   time, after the layer index just finished parsing it once.
 *
 * The ghost pass predates large files being streamed on the print-detail screen
 * and did not distinguish the two, so moving those files onto streaming silently
 * changed its cost model from a memory walk into a full re-parse. On a K2 Plus
 * (dual-core Cortex-A7, 488MB) that meant one core pinned for minutes on a 133MB
 * file starting the instant the index completed, the layer cache thrashed out
 * from under the visible layer, and a UI that could not keep up with touch.
 *
 * The bound below fixes the *work*, not the fidelity: a streamed ghost visits at
 * most kGhostMaxSampledLayers layers however large the file is, spaced evenly
 * so the silhouette still spans the whole model rather than trailing off partway
 * up. Full load is left alone — there, every layer is already free.
 *
 * Kept a pure function in a header, rather than folded into the render thread,
 * for the same reason decide_render_mode() is: the interesting branch is the one
 * that only runs on a streamed file on a device, and it is worth being able to
 * test it without one.
 */

#pragma once

#include "gcode_projection.h"

namespace helix {
namespace gcode {

/**
 * @brief Hard ceiling on layers a *streamed* ghost pass will visit.
 *
 * The budget is normally the height of the ghost buffer (see below); this caps
 * it for an unusually tall canvas so the pass can never grow without limit.
 */
inline constexpr int kGhostMaxSampledLayers = 512;

/**
 * @brief Floor on that budget.
 *
 * A canvas whose height is not known yet, or is very small, still gets enough
 * samples for the silhouette to read as a shape rather than a few strokes.
 */
inline constexpr int kGhostMinSampledLayers = 64;

/**
 * @brief Milliseconds a *streamed* ghost pass sleeps between layers.
 *
 * The pass runs on a background thread that, on the boards streaming exists
 * for, shares two cores with the UI thread. A layer that costs a seek and a
 * parse is long enough that running the whole pass uninterrupted leaves touch
 * unserviced for its duration. Sleeping briefly between layers hands the core
 * back at a known cadence; against the bounded sample above it adds a quarter
 * of a second to a pass that is no longer the thing users are waiting on.
 */
inline constexpr int kGhostStreamYieldMs = 2;

/// Which layers a ghost pass visits: every `step`-th layer, `count` of them.
struct GhostSamplePlan {
    int step = 1;  ///< Stride between visited layers. Always >= 1, never 0.
    int count = 0; ///< How many layers the pass visits. Never negative.
};

/**
 * @brief How many layers a streamed ghost may usefully visit on this canvas.
 *
 * The default view is FRONT, where a layer's Z becomes its screen row: the
 * layers stack up the canvas rather than overlaying each other. Sampling fewer
 * layers than the buffer has rows therefore leaves visible gaps between them -
 * a striped ghost rather than a faded one - while sampling more than that
 * cannot resolve into anything, because two layers that land on the same row
 * draw the same pixels. The rows the buffer has is exactly the useful budget,
 * clamped at both ends.
 *
 * @param resolvable_rows Height of the ghost buffer in pixels. 0 or negative
 *                        when the canvas is not sized yet; the floor covers it.
 */
constexpr int ghost_sample_budget(int resolvable_rows) {
    if (resolvable_rows < kGhostMinSampledLayers) {
        return kGhostMinSampledLayers;
    }
    if (resolvable_rows > kGhostMaxSampledLayers) {
        return kGhostMaxSampledLayers;
    }
    return resolvable_rows;
}

/// Index-scan throughput below which a board is treated as CPU-poor, in MB of
/// G-code per CPU second. A dual Cortex-A7 (CC1, K2) scans about 10; a Pi 4
/// several times that; a desktop hundreds.
inline constexpr double kWeakIndexRateMbPerCpuS = 40.0;

/**
 * @brief Ghost samples per screen row of the model's Z extent on this board.
 *
 * One sample a row is gap-free and differs from drawing every layer in about
 * 0.1% of pixels (a faint speckle on some top faces); two closes most of that
 * at twice the cost. A board that scanned the file's index slower than
 * kWeakIndexRateMbPerCpuS takes one, as does one whose rate is unknown (0).
 *
 * @param index_mb_per_cpu_s GCodeStreamingController::index_throughput_mb_per_cpu_s().
 */
constexpr int ghost_samples_per_z_row(double index_mb_per_cpu_s) {
    return index_mb_per_cpu_s >= kWeakIndexRateMbPerCpuS ? 2 : 1;
}

/// Floor on a model-derived budget. Below the canvas floor because the model's
/// rows are known here: a 2mm-tall part spans a row or two, and drawing it from
/// 64 layers paints the same pixels 30 times over.
inline constexpr int kGhostMinModelSamples = 16;

/// Screen rows a Z span of @p z_span_mm covers in the FRONT view at @p scale
/// pixels per millimetre: Z rises by cos(45deg) per unit (see gcode_projection.h).
constexpr float front_view_z_rows(float z_span_mm, float scale) {
    return z_span_mm > 0.0f && scale > 0.0f ? z_span_mm * projection::kCosE * scale : 0.0f;
}

/**
 * @brief Layer budget for a model whose Z extent spans @p z_rows screen rows.
 *
 * In the FRONT view consecutive layers sit a fraction of a row apart, so each
 * one is painted over by the next everywhere but a sliver of its front edge.
 * The rows the model's own height spans are what can be resolved, which for a
 * flat plate is a small fraction of the canvas. @p samples_per_row per row
 * (ghost_samples_per_z_row()), never below kGhostMinModelSamples and never
 * above the canvas rule.
 *
 * @param canvas_rows     Ghost buffer height in pixels (the canvas rule's input).
 * @param z_rows          front_view_z_rows() of the model; 0 or less when
 *                        unknown, which falls back to the canvas rule.
 * @param samples_per_row Samples per row of Z extent; below 1 counts as 1.
 */
constexpr int ghost_model_budget(int canvas_rows, float z_rows, int samples_per_row) {
    const int canvas_budget = ghost_sample_budget(canvas_rows);
    if (!(z_rows > 0.0f)) {
        return canvas_budget;
    }
    const float wanted_f = z_rows * static_cast<float>(samples_per_row < 1 ? 1 : samples_per_row);
    int wanted = static_cast<int>(wanted_f);
    if (static_cast<float>(wanted) < wanted_f) {
        ++wanted;
    }
    if (wanted < kGhostMinModelSamples) {
        wanted = kGhostMinModelSamples;
    }
    return wanted < canvas_budget ? wanted : canvas_budget;
}

/**
 * @brief Decide the ghost pass's stride for one file against an explicit budget.
 *
 * The stride rule of plan_ghost_sampling() with the budget supplied by the
 * caller, so a budget derived from the model (ghost_model_budget()) shares it.
 * A budget below 1 is treated as 1.
 */
constexpr GhostSamplePlan plan_ghost_sampling_budget(int total_layers, bool streaming, int budget) {
    if (total_layers <= 0) {
        return {1, 0};
    }
    if (budget < 1) {
        budget = 1;
    }

    // Resident parse, or few enough layers that striding would only cost
    // fidelity: visit all of them.
    if (!streaming || total_layers <= budget) {
        return {1, total_layers};
    }

    // Round the stride DOWN, not up. Rounding up lands the count at or under
    // the budget, which sounds right and is the bug: ceil(total / budget) can
    // halve the count (321 layers against a 320 budget gives stride 2 and 161
    // samples), and a model whose projection fills the canvas then has more
    // rows than samples - every other row empty, which is the banding the
    // budget exists to avoid. Flooring guarantees count >= budget whenever the
    // file has that many layers, so every row the model occupies gets ink.
    //
    // The price is that count can exceed the budget, worst case just under
    // double it when total is just under twice the budget. That is still a
    // fixed bound set by the canvas rather than the file, which is the whole
    // point: 5000 layers costs ~334 samples, not 5000.
    const int step = total_layers / budget;
    const int count = (total_layers + step - 1) / step;
    return {step, count};
}

/**
 * @brief Decide the ghost pass's layer budget for one file.
 *
 * @param total_layers    Layer count from the index/parse. May be 0 before an
 *                        index exists; a negative can only be an upstream bug,
 *                        and is treated as empty rather than propagated into a
 *                        loop bound.
 * @param streaming       True when layers come from GCodeStreamingController
 *                        (each visit is a disk seek and parse), false for a
 *                        resident ParsedGCodeFile (a pointer dereference).
 * @param resolvable_rows Height of the ghost buffer in pixels - the most layers
 *                        the projection can draw distinguishably.
 * @return The stride and the number of layers to visit.
 *
 * @note In streaming mode the returned `count` is guaranteed >=
 *       ghost_sample_budget(resolvable_rows) (so no row the model occupies can
 *       be skipped) and < twice it, and `step * (count - 1)` lands within one
 *       stride of the top layer, so the sample spans the model's full height
 *       rather than trailing off partway up.
 */
constexpr GhostSamplePlan plan_ghost_sampling(int total_layers, bool streaming,
                                              int resolvable_rows) {
    return plan_ghost_sampling_budget(total_layers, streaming,
                                      ghost_sample_budget(resolvable_rows));
}

} // namespace gcode
} // namespace helix
