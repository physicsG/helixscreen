// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

/// Register progress_bar XML event callbacks.
/// Must be called before XML components that use <progress_bar> are created.
void ui_progress_bar_init();

namespace helix::ui {

/// One left-to-right pass of the estimate bar's highlight; the next pass starts
/// over at the left.
inline constexpr uint32_t kProgressSweepPeriodMs = 1600;

/// The highlight's place and strength at one moment. Empty (x2 < x1) when the
/// span is too narrow to carry it.
struct SweepBand {
    int32_t x1;
    int32_t x2;
    /// Strength, 0-255: zero at the start and end of a pass, so restarting at
    /// the left never shows as a jump.
    uint8_t strength;
};

/// The highlight over the unfilled span of the bar, from @p span_x1 for
/// @p span_w px, at @p elapsed_ms. The band is a third of the span (at least
/// 8px, never wider than the span) and stays wholly inside it, travelling left
/// to right with ease-in-out and fading in and out at the ends of each pass.
constexpr SweepBand progress_sweep_band(int32_t span_x1, int32_t span_w, uint32_t elapsed_ms) {
    constexpr int32_t kMinBandPx = 8;
    if (span_w < kMinBandPx) {
        return {span_x1, span_x1 - 1, 0};
    }
    const int32_t band_w = span_w / 3 > kMinBandPx ? span_w / 3 : kMinBandPx;
    const float t = static_cast<float>(elapsed_ms % kProgressSweepPeriodMs) /
                    static_cast<float>(kProgressSweepPeriodMs);
    const float u = -2.0f * t + 2.0f;
    const float eased = t < 0.5f ? 4.0f * t * t * t : 1.0f - u * u * u / 2.0f;
    const float centred = 2.0f * t - 1.0f;
    const auto at = static_cast<int32_t>(eased * static_cast<float>(span_w - band_w) + 0.5f);
    const auto strength = static_cast<uint8_t>((1.0f - centred * centred) * 255.0f);
    return {span_x1 + at, span_x1 + at + band_w - 1, strength};
}

} // namespace helix::ui
