// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cmath>

/**
 * @file screensaver_vec.h
 * @brief The 3-vector the 3D savers share, and its arithmetic
 *
 * No LVGL. Every operation is written out per component in x, y, z order, so a saver moving
 * onto it computes the same floats it did by hand.
 */

namespace helix::ui {

struct SaverVec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

constexpr SaverVec3 operator+(SaverVec3 a, SaverVec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

constexpr SaverVec3 operator-(SaverVec3 a, SaverVec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

constexpr SaverVec3 operator*(SaverVec3 a, float k) {
    return {a.x * k, a.y * k, a.z * k};
}

constexpr float dot(SaverVec3 a, SaverVec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr SaverVec3 cross(SaverVec3 a, SaverVec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

constexpr float length_sq(SaverVec3 v) {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

inline float length(SaverVec3 v) {
    return std::sqrt(length_sq(v));
}

/// `v` shortened to at most `max_len` (>= 0); a shorter or zero `v` is returned as it is.
inline SaverVec3 clamp_length(SaverVec3 v, float max_len) {
    const float sq = length_sq(v);
    if (sq <= max_len * max_len || sq == 0.0f) {
        return v;
    }
    return v * (max_len / std::sqrt(sq));
}

/// `v` scaled to unit length, or `fallback` when `v` has none. The fallback is each caller's
/// own: what a degenerate direction should mean differs from one use to the next.
inline SaverVec3 normalized_or(SaverVec3 v, SaverVec3 fallback) {
    const float len = length(v);
    if (len <= 0.0f) {
        return fallback;
    }
    return {v.x / len, v.y / len, v.z / len};
}

} // namespace helix::ui
