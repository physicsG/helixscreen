// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "screensaver_pixel_writer.h"

#include <array>
#include <cstdint>
#include <vector>

/**
 * @file screensaver_stroke_digits.h
 * @brief Digits drawn as strokes along lines and arcs, at any size and stroke weight
 *
 * A digit's centre line lives in a box one glyph unit wide and kStrokeDigitPathH tall (y
 * down). Coverage comes from the distance to that line, so one shape serves the flip clock's
 * bold cards and High Fly's thin numerals. No LVGL.
 */

namespace helix::ui {

/// Height of a digit's centre-line box, in glyph units; its width is 1.
inline constexpr float kStrokeDigitPathH = 1.6f;

/// Distance, in glyph units, from (px, py) to the nearest point of `digit`'s centre line
/// (0 to 9). Box-relative coordinates: (0, 0) is the path box's top left.
float stroke_digit_distance(int digit, float px, float py);

/// Coverage of the ten digits at one size and stroke, each `w` x `h`, 0 to 255, back to back.
struct StrokeDigitSet {
    int32_t w = 0; ///< lround((1 + stroke) * unit)
    int32_t h = 0; ///< lround((kStrokeDigitPathH + stroke) * unit)
    std::vector<uint8_t> coverage;

    /// Coverage of `digit` at (gx, gy) inside its box, 0 outside it or for a digit past 0-9.
    uint8_t at(int digit, int32_t gx, int32_t gy) const;
    /// Bytes held, capacity included.
    size_t heap_bytes() const;
    /// Frees the coverage, capacity included.
    void clear();
};

/**
 * @brief Rasterises the ten digits at `unit` pixels per glyph unit with strokes `stroke` glyph
 * units wide
 *
 * Pixel (x, y) samples the distance at ((x + 0.5) / unit - stroke / 2, (y + 0.5) / unit -
 * stroke / 2) and covers (stroke / 2 - distance) * unit + 0.5, clamped to 0-1. The flip clock
 * calls it with stroke 0.2.
 */
void build_stroke_digits(int32_t unit, float stroke, StrokeDigitSet& out);

/**
 * @brief A soft halo for each of `digits`: its coverage widened by a third of `radius` and
 * blurred by half of it, in boxes `radius` px larger on every side
 *
 * Built once per glyph size; a clock draws it under the digits in a dark colour so pale
 * strokes stay readable over bright clouds.
 */
void build_stroke_halo(const StrokeDigitSet& digits, int32_t radius, StrokeDigitSet& out);

/// The tens and units digits of `value` (0 to 99), as a two-card clock shows them.
constexpr std::array<int, 2> two_digits(int32_t value) {
    return {value / 10 % 10, value % 10};
}

/// Blends `digit` from `set` over the frame with its box's top left at (x, y), `colour` at
/// `opacity` times each pixel's coverage, dithered; pixels off the frame are skipped. `Writer`
/// is a PixelWriter, or anything with its contains() and blend_over().
template <typename Writer>
void blit_stroke_digit(Writer& writer, const StrokeDigitSet& set, int digit, int32_t x, int32_t y,
                       Rgb colour, float opacity) {
    for (int32_t gy = 0; gy < set.h; gy++) {
        for (int32_t gx = 0; gx < set.w; gx++) {
            const uint8_t cov = set.at(digit, gx, gy);
            if (cov == 0 || !writer.contains(x + gx, y + gy)) {
                continue;
            }
            writer.blend_over(x + gx, y + gy, colour, opacity * static_cast<float>(cov) / 255.0f);
        }
    }
}

} // namespace helix::ui
