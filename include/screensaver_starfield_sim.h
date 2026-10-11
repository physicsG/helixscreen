// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef HELIX_ENABLE_SCREENSAVER

#include "screensaver_frame.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace helix::ui {

/**
 * @brief The starfield's stars, and how one frame of them is drawn
 *
 * No LVGL, no clock, no shared random sequence and no allocation after init(), so it can
 * be stepped and checked without a display.
 */
class StarfieldSim {
  public:
    static constexpr int NUM_STARS = 150;

    struct Star {
        float x;        // normalized position (-1..1)
        float y;        // normalized position (-1..1)
        float z;        // depth (0..1, 1=far, approaches 0)
        float speed;    // z decrement per 33 ms
        uint8_t tint_r; // color tint (assigned at birth, visible when close)
        uint8_t tint_g;
        uint8_t tint_b;
        // Where the star was drawn in the frame, for the next step's erase
        int16_t prev_sx;
        int16_t prev_sy;
        uint8_t prev_size; // 0 = not drawn
    };

    /// Places NUM_STARS stars for a w x h frame, drawing from `rng`.
    void init(uint32_t w, uint32_t h, std::minstd_rand& rng);

    /**
     * @brief Stars moved and drawn per frame: the first `count` of the population
     *
     * `count` is clamped to [1, NUM_STARS]. The population keeps its allocation, and a
     * star past the count is erased on the next step and then parked, so the ladder that
     * changes it reallocates nothing per frame.
     */
    void set_active_count(int count);

    /// The count set by set_active_count(), NUM_STARS until one is set.
    int active_count() const {
        return active_count_;
    }

    /**
     * @brief Draws the next frame into `target`
     *
     * Erases the stars where the previous step drew them, moves each active star by
     * `dt_ms` of flight and draws it, recycling a star that reaches the camera or leaves
     * the frame.
     *
     * @param dirty Replaced with one box per star that erased or drew anything: the union
     * of that star's erase box and its draw box, clipped to the frame
     */
    void step(uint32_t dt_ms, FrameTarget& target, std::minstd_rand& rng,
              std::vector<DirtyRect>& dirty);

    std::vector<Star>& stars() {
        return stars_;
    }

    const std::vector<Star>& stars() const {
        return stars_;
    }

  private:
    void recycle(Star& star, std::minstd_rand& rng);

    std::vector<Star> stars_;
    /// Per-star dirty box of the step in flight, sized by init().
    std::vector<DirtyRect> star_dirty_;
    int active_count_ = NUM_STARS;
    float cx_ = 0;    // frame center X
    float cy_ = 0;    // frame center Y
    float focal_ = 0; // projection focal length
};

} // namespace helix::ui

#endif // HELIX_ENABLE_SCREENSAVER
