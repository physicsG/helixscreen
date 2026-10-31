// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "calibration_types.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

/**
 * @file screw_tilt_surface.h
 * @brief The bed surface implied by SCREWS_TILT_CALCULATE's probe points
 *
 * Four corner screws describe a bilinear patch, which is exactly a bed that is
 * both tilted and twisted; three describe a plane. With more screws the patch is
 * a least-squares fit. The panel shades it with the bed-mesh colour scale so the
 * tilt reads at a glance.
 */

namespace helix {

struct ScrewTiltSurface {
    bool valid = false;
    float min_x = 0.0f; ///< Bed bounds spanned by the screws (mm)
    float max_x = 0.0f;
    float min_y = 0.0f;
    float max_y = 0.0f;
    /// z = c0 + c1*u + c2*v + c3*u*v, with u/v the position across the bounds in [0, 1]
    std::array<double, 4> coef{};
    double spread_mm = 0.0; ///< Highest probed height minus the lowest
    double color_min = 0.0; ///< Heights at the two ends of the colour scale
    double color_max = 0.0;

    /// Height at (u, v); u runs left to right, v front to rear.
    [[nodiscard]] double z_at(double u, double v) const {
        return coef[0] + coef[1] * u + coef[2] * v + coef[3] * u * v;
    }
};

namespace screw_tilt_surface_detail {

/// Solves the n x n system in place by Gaussian elimination with partial pivoting.
/// False when the system is singular, i.e. the points cannot pin the surface down.
inline bool solve(std::array<std::array<double, 5>, 4>& m, size_t n, std::array<double, 4>& out) {
    for (size_t col = 0; col < n; col++) {
        size_t pivot = col;
        for (size_t r = col + 1; r < n; r++) {
            if (std::abs(m[r][col]) > std::abs(m[pivot][col])) {
                pivot = r;
            }
        }
        if (std::abs(m[pivot][col]) < 1e-9) {
            return false;
        }
        std::swap(m[col], m[pivot]);
        for (size_t r = 0; r < n; r++) {
            if (r == col) {
                continue;
            }
            const double f = m[r][col] / m[col][col];
            for (size_t c = col; c <= n; c++) {
                m[r][c] -= f * m[col][c];
            }
        }
    }
    out.fill(0.0);
    for (size_t i = 0; i < n; i++) {
        out[i] = m[i][n] / m[i][i];
    }
    return true;
}

} // namespace screw_tilt_surface_detail

/**
 * @brief Fit the bed surface through the probed screw heights
 *
 * @param min_half_range_mm Floor on half the colour scale's span. Without it a
 *        bed already inside tolerance would stretch its few microns across the
 *        whole scale and look as alarming as a badly tilted one.
 * @return valid == false for fewer than three screws, or screws in a line
 */
[[nodiscard]] inline ScrewTiltSurface
fit_screw_tilt_surface(const std::vector<ScrewTiltResult>& results, double min_half_range_mm) {
    ScrewTiltSurface s;
    if (results.size() < 3) {
        return s;
    }

    s.min_x = s.max_x = results[0].x_pos;
    s.min_y = s.max_y = results[0].y_pos;
    double z_lo = results[0].z_height;
    double z_hi = results[0].z_height;
    for (const auto& r : results) {
        s.min_x = std::min(s.min_x, r.x_pos);
        s.max_x = std::max(s.max_x, r.x_pos);
        s.min_y = std::min(s.min_y, r.y_pos);
        s.max_y = std::max(s.max_y, r.y_pos);
        z_lo = std::min(z_lo, static_cast<double>(r.z_height));
        z_hi = std::max(z_hi, static_cast<double>(r.z_height));
    }
    const double span_x = s.max_x - s.min_x;
    const double span_y = s.max_y - s.min_y;
    if (span_x <= 0.0 || span_y <= 0.0) {
        return s;
    }

    // Three points pin a plane; the twist term needs a fourth.
    const size_t n = results.size() >= 4 ? 4 : 3;
    std::array<std::array<double, 5>, 4> m{};
    for (const auto& r : results) {
        const double u = (r.x_pos - s.min_x) / span_x;
        const double v = (r.y_pos - s.min_y) / span_y;
        const std::array<double, 4> basis{1.0, u, v, u * v};
        for (size_t i = 0; i < n; i++) {
            for (size_t j = 0; j < n; j++) {
                m[i][j] += basis[i] * basis[j];
            }
            m[i][n] += basis[i] * r.z_height;
        }
    }
    if (!screw_tilt_surface_detail::solve(m, n, s.coef)) {
        return s;
    }

    const double center = (z_lo + z_hi) / 2.0;
    const double half = std::max((z_hi - z_lo) / 2.0, min_half_range_mm);
    s.spread_mm = z_hi - z_lo;
    s.color_min = center - half;
    s.color_max = center + half;
    s.valid = true;
    return s;
}

} // namespace helix
