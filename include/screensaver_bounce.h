// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef HELIX_ENABLE_SCREENSAVER

#include "screensaver_base.h"
#include "screensaver_motion.h"

#include <cmath>
#include <cstdint>
#include <lvgl.h>
#include <optional>
#include <random>
#include <vector>

namespace helix::screensaver_bounce {

/// Room the sprite must leave on each axis for the bounce to read as a bounce.
inline constexpr int MIN_RANGE_PX = 48;

/// Sprite edge as a fraction of the screen's narrow axis.
inline constexpr float SPRITE_FRACTION = 0.28f;

/**
 * @brief Longest sprite edge for a screen, or 0 when the screen cannot host a bounce
 *
 * Sized off the narrow axis, then held below what would leave less than
 * MIN_RANGE_PX of travel. 480x320 yields a 90px box over a 390x230 range.
 */
inline int sprite_size_for(int screen_w, int screen_h) {
    const int narrow = (screen_w < screen_h) ? screen_w : screen_h;
    int target = static_cast<int>(std::lround(SPRITE_FRACTION * static_cast<float>(narrow)));
    if (target > 320) {
        target = 320;
    }
    const int ceiling = narrow - MIN_RANGE_PX;
    if (target > ceiling) {
        target = ceiling;
    }
    return (target < 48) ? 0 : target;
}

/**
 * @brief Fit a source image's aspect ratio inside a square box of @p box pixels
 *
 * The walls are the edges of the sprite's own rectangle, so that rectangle has
 * to be the shape of the artwork. A square box around a tall printer render
 * would hold a column of empty pixels on each side and the printer would turn
 * before reaching the screen edge.
 */
inline void fit_sprite(int box, int32_t src_w, int32_t src_h, int& out_w, int& out_h) {
    if (src_w <= 0 || src_h <= 0) {
        out_w = box;
        out_h = box;
        return;
    }
    const double scale =
        static_cast<double>(box) / static_cast<double>((src_w >= src_h) ? src_w : src_h);
    out_w = static_cast<int>(std::lround(static_cast<double>(src_w) * scale));
    out_h = static_cast<int>(std::lround(static_cast<double>(src_h) * scale));
    if (out_w < 1) {
        out_w = 1;
    }
    if (out_h < 1) {
        out_h = 1;
    }
}

} // namespace helix::screensaver_bounce

namespace helix {

/**
 * @brief Bouncing Printer screensaver
 *
 * The printer this display is attached to drifts across a black field and
 * reflects off the edges, changing tint on every wall. Landing a true corner
 * earns a celebration: the backdrop flashes and, above the ladder's lowest
 * level on a board that animates, a confetti burst.
 *
 * Position is computed from elapsed time rather than integrated per frame,
 * matching the rest of the subsystem, so a level that changes the frame period
 * changes nothing on screen but the frame rate.
 */
class BouncingPrinterScreensaver : public helix::ui::SaverBase {
  public:
    BouncingPrinterScreensaver() = default;
    BouncingPrinterScreensaver(const BouncingPrinterScreensaver&) = delete;
    BouncingPrinterScreensaver& operator=(const BouncingPrinterScreensaver&) = delete;

    ScreensaverType type() const override {
        return ScreensaverType::BOUNCING_PRINTER;
    }

  protected:
    /// The sprite is an LVGL image on the overlay; there is no canvas.
    std::optional<lv_color_format_t> canvas_format() const override {
        return std::nullopt;
    }
    bool on_start() override;
    void on_frame(uint32_t dt_ms, std::vector<helix::ui::DirtyRect>& dirty) override;
    void on_stop() override;
    size_t ladder_size() const override {
        return sizeof(LEVEL_PERIODS_MS) / sizeof(LEVEL_PERIODS_MS[0]);
    }
    uint32_t ladder_period_ms(size_t level) const override {
        return LEVEL_PERIODS_MS[level];
    }

  private:
    friend class BounceTestAccess;

    /// Frame period per level: 16 ms with confetti corners, 33 ms with confetti corners, 33 ms
    /// with the corner flash alone.
    static constexpr uint32_t LEVEL_PERIODS_MS[] = {helix::ui::SAVER_FAST_PERIOD, 33, 33};
    /// First level that celebrates a corner with the flash alone.
    static constexpr size_t FLASH_ONLY_LEVEL = 2;

    /// Decode the active printer image into a persistent draw buffer
    bool decode_sprite();
    void free_sprite();
    /// Pick a velocity whose path is not a short repeating loop
    void seed_motion();
    /// Size the sprite to `box` and derive travel ranges and speed from the current screen
    void apply_geometry(int box);
    /// Recompute travel ranges after a resolution change without teleporting
    void rebase(int screen_w, int screen_h);
    void apply_tint();
    /// Confetti particles a corner releases at `level`: full bursts, none on the flash-only rung.
    int confetti_count(size_t level) const;

    lv_obj_t* img_ = nullptr;
    lv_draw_buf_t* decoded_ = nullptr;

    uint32_t elapsed_ms_ = 0;

    int screen_w_ = 0;
    int screen_h_ = 0;
    int sprite_w_ = 0;
    int sprite_h_ = 0;
    int32_t src_w_ = 0;
    int32_t src_h_ = 0;

    // Speed (px/s) the path travels, shared by both axes
    float speed_ = 0.0f;

    // Travel range per axis: screen extent less the sprite footprint
    float range_x_ = 0.0f;
    float range_y_ = 0.0f;

    // Phase offset and velocity (px/s) of the unbounded path
    float x0_ = 0.0f;
    float y0_ = 0.0f;
    float vx_ = 0.0f;
    float vy_ = 0.0f;

    int prev_fold_x_ = 0;
    int prev_fold_y_ = 0;
    int32_t prev_x_ = INT32_MIN;
    int32_t prev_y_ = INT32_MIN;

    int color_index_ = 0;
    int corner_flash_ticks_ = 0;
};

} // namespace helix

#endif // HELIX_ENABLE_SCREENSAVER
