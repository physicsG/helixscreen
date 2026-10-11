// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "screensaver_frame.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace helix::ui {

/// 4x4 ordered-dither thresholds, 0 to 15, indexed [y & 3][x & 3].
inline constexpr uint8_t BAYER_4X4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

/**
 * @brief An 8-bit channel quantized to 0..max_level at dither threshold `threshold` (0-15)
 *
 * floor(c * max_level / 255 + (2 * threshold + 1) / 32): over a 4x4 tile the levels average
 * back to the 8-bit value.
 */
constexpr uint8_t dither_channel(uint8_t c, uint8_t max_level, uint8_t threshold) {
    const uint32_t scaled =
        static_cast<uint32_t>(c) * max_level * 32u + (2u * threshold + 1u) * 255u;
    return static_cast<uint8_t>(std::min<uint32_t>(max_level, scaled / (255u * 32u)));
}

/**
 * @brief Writes colours into a FrameTarget
 *
 * No LVGL: the frame's format is data, so the host test build exercises every format
 * whatever LV_COLOR_DEPTH the app is built with. Callers keep coordinates inside the frame,
 * which contains() answers; line() visits points without that check.
 */
class PixelWriter {
  public:
    explicit PixelWriter(const FrameTarget& frame) : frame_(frame) {}

    bool contains(int32_t x, int32_t y) const {
        return x >= 0 && y >= 0 && x < static_cast<int32_t>(frame_.w) &&
               y < static_cast<int32_t>(frame_.h);
    }

    /// Writes `c` at (x, y). XRGB8888 stores B, G, R and an X byte of 0xFF; RGB565 keeps each
    /// channel's top bits.
    void put(int32_t x, int32_t y, Rgb c) {
        if (frame_.format == PixelFormat::RGB565) {
            write_565(x, y, static_cast<uint16_t>((c.r >> 3) << 11 | (c.g >> 2) << 5 | (c.b >> 3)));
            return;
        }
        uint8_t* p = pixel(x, y, 4);
        p[0] = c.b;
        p[1] = c.g;
        p[2] = c.r;
        p[3] = 0xFF;
    }

    /// Like put(), but RGB565 dithers each channel with BAYER_4X4 at (x, y), so a smooth fade
    /// shows as a fine pattern instead of bands. XRGB8888 is written exactly.
    void put_dithered(int32_t x, int32_t y, Rgb c) {
        if (frame_.format != PixelFormat::RGB565) {
            put(x, y, c);
            return;
        }
        const uint8_t t = BAYER_4X4[y & 3][x & 3];
        write_565(x, y,
                  static_cast<uint16_t>(dither_channel(c.r, 31, t) << 11 |
                                        dither_channel(c.g, 63, t) << 5 |
                                        dither_channel(c.b, 31, t)));
    }

    /// The colour at (x, y). RGB565 levels expand to 8 bits by repeating their top bits.
    Rgb get(int32_t x, int32_t y) const {
        if (frame_.format == PixelFormat::RGB565) {
            const uint8_t* p = pixel(x, y, 2);
            const auto v = static_cast<uint16_t>(p[0] | p[1] << 8);
            const auto r5 = static_cast<uint8_t>(v >> 11 & 0x1F);
            const auto g6 = static_cast<uint8_t>(v >> 5 & 0x3F);
            const auto b5 = static_cast<uint8_t>(v & 0x1F);
            return {static_cast<uint8_t>(r5 << 3 | r5 >> 2),
                    static_cast<uint8_t>(g6 << 2 | g6 >> 4),
                    static_cast<uint8_t>(b5 << 3 | b5 >> 2)};
        }
        const uint8_t* p = pixel(x, y, 4);
        return {p[2], p[1], p[0]};
    }

    /// Raises each channel at (x, y) to at least `c`'s, so overlapping light shows its brightest
    /// part. A colour no brighter in any channel leaves the pixel untouched.
    void blend_max(int32_t x, int32_t y, Rgb c) {
        const Rgb current = get(x, y);
        if (c.r <= current.r && c.g <= current.g && c.b <= current.b) {
            return;
        }
        put_dithered(
            x, y, {std::max(c.r, current.r), std::max(c.g, current.g), std::max(c.b, current.b)});
    }

    /// Writes `c` over every pixel, leaving row padding alone.
    void fill(Rgb c) {
        for (int32_t y = 0; y < static_cast<int32_t>(frame_.h); y++) {
            for (int32_t x = 0; x < static_cast<int32_t>(frame_.w); x++) {
                put(x, y, c);
            }
        }
    }

    /**
     * @brief Visits every point of the line from (x0, y0) to (x1, y1), both ends included
     *
     * Bresenham's algorithm, in order from the first end: each step moves at most one pixel on
     * each axis. Points outside any frame are visited too.
     */
    template <typename Plot>
    static void line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, Plot&& plot) {
        const int32_t dx = std::abs(x1 - x0);
        const int32_t sx = x0 < x1 ? 1 : -1;
        const int32_t dy = -std::abs(y1 - y0);
        const int32_t sy = y0 < y1 ? 1 : -1;
        int32_t err = dx + dy;
        for (;;) {
            plot(x0, y0);
            if (x0 == x1 && y0 == y1) {
                return;
            }
            const int32_t e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }

  private:
    uint8_t* pixel(int32_t x, int32_t y, size_t bytes) const {
        return frame_.data + static_cast<size_t>(y) * frame_.stride +
               static_cast<size_t>(x) * bytes;
    }

    void write_565(int32_t x, int32_t y, uint16_t v) {
        uint8_t* p = pixel(x, y, 2);
        p[0] = static_cast<uint8_t>(v & 0xFF);
        p[1] = static_cast<uint8_t>(v >> 8);
    }

    FrameTarget frame_;
};

} // namespace helix::ui
