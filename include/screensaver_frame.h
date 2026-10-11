// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * @file screensaver_frame.h
 * @brief Frame geometry the screensavers share. No LVGL, so simulations use it too.
 */

namespace helix::ui {

/// Frame period, in ms, of every saver's most expensive level: one refresh of a 60 Hz panel.
inline constexpr uint32_t SAVER_FAST_PERIOD = 16;

/**
 * @brief Most areas a saver invalidates in one frame
 *
 * LVGL keeps pending invalid areas in a fixed buffer (LV_INV_BUF_SIZE, raised to 192 in
 * lv_conf.h) and invalidates the whole screen once it overflows, so a frame with more
 * areas than that costs a full redraw. Savers invalidate one area per moving object, so
 * the cap sits above the starfield's full population (150 stars) and leaves the rest of
 * the buffer for UI invalidations.
 */
inline constexpr size_t SAVER_MAX_DIRTY_AREAS = 160;

/**
 * @brief Dirty coverage at or above which invalidating the whole canvas is cheaper
 *
 * A partial invalidation costs MORE than a full one once it covers nearly everything: a
 * double-buffered display copies the untouched remainder out of the previous buffer in strips
 * every frame, while a full-canvas invalidation leaves it nothing to sync.
 */
inline constexpr int64_t SAVER_WHOLE_CANVAS_PERCENT = 90;

/// How a FrameTarget stores its pixels.
enum class PixelFormat : uint8_t {
    /// 4 bytes per pixel: B, G, R, and an X byte of 0xFF
    XRGB8888,
    /// 2 bytes per pixel, little-endian: red in the top 5 bits, green in the middle 6, blue in the
    /// low 5
    RGB565,
};

/// An 8-bit-per-channel colour.
struct Rgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    bool operator==(const Rgb& o) const {
        return r == o.r && g == o.g && b == o.b;
    }
};

/// A frame a saver writes pixels into directly: rows start `stride` bytes apart.
struct FrameTarget {
    uint8_t* data = nullptr;
    uint32_t stride = 0;
    uint32_t w = 0;
    uint32_t h = 0;
    PixelFormat format = PixelFormat::XRGB8888;
};

/// Inclusive pixel bounds of what changed in a frame. Empty until something is added.
struct DirtyRect {
    int32_t x1 = 0;
    int32_t y1 = 0;
    int32_t x2 = -1;
    int32_t y2 = -1;

    constexpr bool empty() const {
        return x2 < x1 || y2 < y1;
    }

    /// Pixels covered, 0 when empty.
    constexpr int64_t area() const {
        return empty() ? 0 : static_cast<int64_t>(x2 - x1 + 1) * (y2 - y1 + 1);
    }

    /// Grows to cover the inclusive box (ax1, ay1)-(ax2, ay2). An empty box adds nothing.
    void add(int32_t ax1, int32_t ay1, int32_t ax2, int32_t ay2) {
        if (ax2 < ax1 || ay2 < ay1) {
            return;
        }
        if (empty()) {
            *this = {ax1, ay1, ax2, ay2};
            return;
        }
        x1 = std::min(x1, ax1);
        y1 = std::min(y1, ay1);
        x2 = std::max(x2, ax2);
        y2 = std::max(y2, ay2);
    }

    void add(const DirtyRect& other) {
        add(other.x1, other.y1, other.x2, other.y2);
    }

    bool operator==(const DirtyRect& o) const {
        return x1 == o.x1 && y1 == o.y1 && x2 == o.x2 && y2 == o.y2;
    }
};

/// True when `covered_px` covered pixels of a `w` x `h` canvas are enough of it that
/// invalidating all of it is cheaper than invalidating the part that changed. Coverage
/// counts covered pixels, not the span of the areas: each area is rendered and flushed on
/// its own, so scattered small areas cost their sum however far apart they lie.
constexpr bool covers_whole_canvas(int64_t covered_px, int32_t w, int32_t h) {
    const int64_t canvas = static_cast<int64_t>(w) * static_cast<int64_t>(h);
    if (canvas <= 0) {
        return false;
    }
    return covered_px * 100 >= canvas * SAVER_WHOLE_CANVAS_PERCENT;
}

/// One-box form of the coverage rule: a single box covers its own pixels.
constexpr bool covers_whole_canvas(const DirtyRect& bounds, int32_t w, int32_t h) {
    return covers_whole_canvas(bounds.area(), w, h);
}

/**
 * @brief Reduces `areas` to at most `max_areas` boxes that together cover every input box
 *
 * Empty boxes are dropped, and within the limit the rest keep their order. Past it, the pair
 * whose bounding box adds the fewest pixels over the two boxes is joined, until the limit
 * holds. A `max_areas` of 0 is read as 1.
 */
void merge_dirty_areas(std::vector<DirtyRect>& areas, size_t max_areas);

} // namespace helix::ui
