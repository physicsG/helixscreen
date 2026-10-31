// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * @file screensaver_frame.h
 * @brief Frame geometry the screensavers share. No LVGL, so simulations use it too.
 */

namespace helix::ui {

/// Frame period, in ms, of every saver's most expensive level: one refresh of a 60 Hz panel.
inline constexpr uint32_t kSaverFastPeriod = 16;

/**
 * @brief Most areas a saver invalidates in one frame
 *
 * LVGL keeps pending invalid areas in a fixed buffer (LV_INV_BUF_SIZE, raised to 192 in
 * lv_conf.h) and invalidates the whole screen once it overflows, so a frame with more
 * areas than that costs a full redraw. Savers invalidate one area per moving object, so
 * the cap sits above the starfield's full population (150 stars) and leaves the rest of
 * the buffer for UI invalidations.
 */
inline constexpr size_t kSaverMaxDirtyAreas = 160;

/**
 * @brief Dirty coverage at or above which invalidating the whole canvas is cheaper
 *
 * A partial invalidation costs MORE than a full one once it covers nearly everything: a
 * double-buffered display copies the untouched remainder out of the previous buffer in strips
 * every frame, while a full-canvas invalidation leaves it nothing to sync.
 */
inline constexpr int64_t kSaverWholeCanvasPercent = 90;

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

    /// True when the two share a pixel. Nothing intersects an empty box.
    constexpr bool intersects(const DirtyRect& o) const {
        return !empty() && !o.empty() && x1 <= o.x2 && o.x1 <= x2 && y1 <= o.y2 && o.y1 <= y2;
    }

    bool operator==(const DirtyRect& o) const {
        return x1 == o.x1 && y1 == o.y1 && x2 == o.x2 && y2 == o.y2;
    }

    /// Every pixel of a `w` x `h` frame.
    static constexpr DirtyRect whole(int32_t w, int32_t h) {
        return {0, 0, w - 1, h - 1};
    }

    /// The part inside a `w` x `h` frame; empty when none of it is.
    constexpr DirtyRect clipped(int32_t w, int32_t h) const {
        if (empty()) {
            return {};
        }
        const DirtyRect c{std::max<int32_t>(x1, 0), std::max<int32_t>(y1, 0), std::min(x2, w - 1),
                          std::min(y2, h - 1)};
        return c.empty() ? DirtyRect{} : c;
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
    return covered_px * 100 >= canvas * kSaverWholeCanvasPercent;
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

/**
 * @brief A frame's dirty list as the canvas invalidates it
 *
 * merge_dirty_areas() to `max_areas`, then, when what is left covers enough of a `w` x `h`
 * frame (covers_whole_canvas()), the one box DirtyRect::whole(w, h). A saver that needs this
 * decision before the canvas makes it (to redraw text over what changed) calls it itself; the
 * canvas reaches the same answer from the reduced list.
 */
void reduce_dirty(std::vector<DirtyRect>& areas, size_t max_areas, int32_t w, int32_t h);

/// Pixels a `px`-pixel extent takes when every `scale` (> 0) pixels draw as one, rounded up.
constexpr int32_t scaled_extent(int32_t px, int32_t scale) {
    return (px + scale - 1) / scale;
}

/// `v` rounded to the nearest pixel, halves away from zero.
inline int32_t round_px(float v) {
    return static_cast<int32_t>(std::lround(v));
}

/**
 * @brief Row `y` of `frame` as pixels of type `Px`: uint16_t for RGB565, uint32_t for XRGB8888
 *
 * A word's bytes are the frame's bytes only on a little-endian CPU, which every target is.
 */
template <typename Px> Px* frame_row(const FrameTarget& frame, int32_t y) {
    static_assert(sizeof(Px) == 2 || sizeof(Px) == 4, "a frame pixel is 2 or 4 bytes");
    static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
                  "pixels are written as little-endian words");
    return reinterpret_cast<Px*>(frame.data + static_cast<size_t>(y) * frame.stride);
}

/// Calls `f` with a value of `format`'s pixel type, uint16_t{} for RGB565 or uint32_t{} for
/// XRGB8888, so a drawing loop is written once as a template over that type.
template <typename F> decltype(auto) with_pixel_type(PixelFormat format, F&& f) {
    if (format == PixelFormat::RGB565) {
        return f(uint16_t{});
    }
    return f(uint32_t{});
}

} // namespace helix::ui
