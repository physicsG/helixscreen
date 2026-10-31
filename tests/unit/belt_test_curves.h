// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../../include/belt_tension_types.h"

#include <utility>
#include <vector>

/**
 * @file belt_test_curves.h
 * @brief Synthetic resonance curves shared by the belt comparison and tuning tests
 */

namespace helix::calibration::test {

/// Lorentzian peaks (centre Hz, height) on a flat floor, bins every bin_hz from
/// lo up to (not including) hi. Klipper's resonance CSVs space bins 3200/4096 Hz
/// apart.
inline BeltCurve lorentzian_curve(std::vector<std::pair<float, float>> peaks_hz_height, float lo,
                                  float hi, float bin_hz = 3200.0f / 4096.0f, float floor = 0.01f,
                                  float half_width_hz = 5.0f) {
    BeltCurve c;
    for (float f = lo; f < hi; f += bin_hz) {
        float v = floor;
        for (const auto& [peak_hz, height] : peaks_hz_height) {
            const float d = (f - peak_hz) / half_width_hz;
            v += height / (1.0f + d * d);
        }
        c.emplace_back(f, v);
    }
    return c;
}

/// What a sweep limited to `window` reads: the full curve's bins inside the
/// window, and the unexcited floor everywhere else.
inline BeltCurve windowed(const BeltCurve& full, BeltSweepWindow window, float floor) {
    BeltCurve c = full;
    for (auto& [f, v] : c) {
        if (f < window.freq_start_hz || f > window.freq_end_hz) {
            v = floor;
        }
    }
    return c;
}

/// A response whose in-plane curve is `in_plane` and whose psd_xyz is
/// in_plane / in_plane_share at every bin.
inline BeltResponse response_from(const BeltCurve& in_plane, float in_plane_share = 0.9f) {
    BeltResponse r;
    r.in_plane = in_plane;
    r.xyz = in_plane;
    for (auto& bin : r.xyz) {
        bin.second /= in_plane_share;
    }
    return r;
}

/// windowed() applied to both curves of a response.
inline BeltResponse windowed(const BeltResponse& full, BeltSweepWindow window, float floor) {
    return BeltResponse{windowed(full.xyz, window, floor), windowed(full.in_plane, window, floor)};
}

/// Every bin of both curves moved by `hz`: a capture's shape at another tension.
inline BeltResponse shifted(const BeltResponse& r, float hz) {
    BeltResponse out = r;
    for (BeltCurve* c : {&out.xyz, &out.in_plane}) {
        for (auto& bin : *c) {
            bin.first += hz;
        }
    }
    return out;
}

} // namespace helix::calibration::test
