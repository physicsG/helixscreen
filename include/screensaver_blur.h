// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <vector>

/**
 * @file screensaver_blur.h
 * @brief Box blur over a float grid, the savers' one blur. No LVGL.
 */

namespace helix::ui {

/**
 * @brief Box blur of radius `r` over the row-major `w` x `h` grid `a`, in place
 *
 * Each pass averages along x, then along y; three passes come close to a Gaussian. Each axis
 * wraps or clamps at its ends. `tmp` holds w x h floats of scratch.
 */
void box_blur(float* a, int32_t w, int32_t h, int32_t r, bool wrap_x, bool wrap_y, float* tmp,
              int passes = 3);

/// box_blur() on a vector, sizing `tmp` to match.
inline void box_blur(std::vector<float>& a, int32_t w, int32_t h, int32_t r, bool wrap_x,
                     bool wrap_y, std::vector<float>& tmp, int passes = 3) {
    tmp.resize(a.size());
    box_blur(a.data(), w, h, r, wrap_x, wrap_y, tmp.data(), passes);
}

} // namespace helix::ui
