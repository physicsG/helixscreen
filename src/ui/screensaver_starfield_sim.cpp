// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_ENABLE_SCREENSAVER

#include "screensaver_starfield_sim.h"

#include "screensaver_motion.h"
#include "screensaver_pixel_writer.h"

#include <cmath>

namespace helix::ui {

namespace {

using screensaver::random_below;
using screensaver::unit_random;

// A star's speed is its depth change per this many ms of frame time
constexpr float SPEED_FRAME_MS = 33.0f;
constexpr float COLOR_THRESHOLD = 0.35f; // stars closer than this show color
/// Depth a star is recycled at, and the span of depths a live star occupies. A drawn star
/// always sits in [Z_NEAR, 1.0], so size and brightness scale over Z_SPAN.
constexpr float Z_NEAR = 0.01f;
constexpr float Z_SPAN = 1.0f - Z_NEAR;

constexpr Rgb BLACK = {0, 0, 0};

// Star color tints: blue dwarfs, red giants, yellow suns, blue-white hot stars
constexpr uint8_t STAR_TINTS[][3] = {
    {255, 255, 255}, // white (most common)
    {255, 255, 255}, // white
    {255, 255, 255}, // white
    {255, 200, 150}, // warm yellow
    {255, 160, 120}, // orange
    {255, 120, 100}, // red giant
    {150, 180, 255}, // blue dwarf
    {200, 220, 255}, // blue-white
};
constexpr int NUM_TINTS = sizeof(STAR_TINTS) / sizeof(STAR_TINTS[0]);

void assign_tint(std::minstd_rand& rng, uint8_t& r, uint8_t& g, uint8_t& b) {
    int idx = random_below(rng, NUM_TINTS);
    r = STAR_TINTS[idx][0];
    g = STAR_TINTS[idx][1];
    b = STAR_TINTS[idx][2];
}

/// Paints the size x size square at (sx, sy), clipped to the frame, and returns the box it
/// wrote (empty when the square is entirely outside the frame).
DirtyRect fill_square(PixelWriter& writer, const FrameTarget& target, int sx, int sy, int size,
                      Rgb color) {
    const int x1 = std::max(sx, 0);
    const int y1 = std::max(sy, 0);
    const int x2 = std::min(sx + size, static_cast<int>(target.w)) - 1;
    const int y2 = std::min(sy + size, static_cast<int>(target.h)) - 1;
    if (x2 < x1 || y2 < y1) {
        return {};
    }
    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            writer.put(x, y, color);
        }
    }
    return {x1, y1, x2, y2};
}

} // namespace

void StarfieldSim::init(uint32_t w, uint32_t h, std::minstd_rand& rng) {
    cx_ = static_cast<float>(w) / 2.0f;
    cy_ = static_cast<float>(h) / 2.0f;
    focal_ = static_cast<float>(w) / 3.0f;

    stars_.resize(NUM_STARS);
    star_dirty_.resize(NUM_STARS);
    active_count_ = NUM_STARS;
    for (auto& star : stars_) {
        float angle = unit_random(rng) * 2.0f * 3.14159265f;
        float radius = 0.1f + unit_random(rng) * 0.9f;
        star.x = radius * std::cos(angle);
        star.y = radius * std::sin(angle);
        star.z = Z_NEAR + unit_random(rng) * Z_SPAN;
        star.speed = 0.008f + unit_random(rng) * 0.017f;
        assign_tint(rng, star.tint_r, star.tint_g, star.tint_b);
        star.prev_sx = 0;
        star.prev_sy = 0;
        star.prev_size = 0;
    }
}

void StarfieldSim::recycle(Star& star, std::minstd_rand& rng) {
    // Pick random angle + radius so stars fly uniformly in all directions
    float angle = unit_random(rng) * 2.0f * 3.14159265f;
    float radius = 0.3f + unit_random(rng) * 0.7f;
    star.x = radius * std::cos(angle);
    star.y = radius * std::sin(angle);
    star.z = 1.0f;
    star.speed = 0.008f + unit_random(rng) * 0.017f;
    assign_tint(rng, star.tint_r, star.tint_g, star.tint_b);
}

void StarfieldSim::set_active_count(int count) {
    active_count_ = std::clamp(count, 1, NUM_STARS);
}

void StarfieldSim::step(uint32_t dt_ms, FrameTarget& target, std::minstd_rand& rng,
                        std::vector<DirtyRect>& dirty) {
    dirty.clear();
    PixelWriter writer(target);
    const int w = static_cast<int>(target.w);
    const int h = static_cast<int>(target.h);
    star_dirty_.resize(stars_.size());

    // Erase previous star positions (an incremental clear, which avoids a full-frame fill).
    // Covers every star of the population, so a star parked by set_active_count() leaves
    // the canvas on the step after it stops flying.
    for (size_t i = 0; i < stars_.size(); i++) {
        star_dirty_[i] = DirtyRect{};
        Star& star = stars_[i];
        if (star.prev_size == 0) {
            continue;
        }
        star_dirty_[i].add(
            fill_square(writer, target, star.prev_sx, star.prev_sy, star.prev_size, BLACK));
        star.prev_size = 0;
    }

    const float frames = static_cast<float>(dt_ms) / SPEED_FRAME_MS;
    // active_count_ is a ladder setting, so it can outlive the population it was set for.
    const int flying = std::min(active_count_, static_cast<int>(stars_.size()));
    for (int i = 0; i < flying; i++) {
        Star& star = stars_[i];

        // Move star closer by its speed, scaled to the time since the previous frame
        star.z -= star.speed * frames;

        if (star.z <= Z_NEAR) {
            recycle(star, rng);
            continue;
        }

        // Project to frame coordinates
        float sx = cx_ + (star.x / star.z) * focal_;
        float sy = cy_ + (star.y / star.z) * focal_;

        if (sx < 0 || sx >= w || sy < 0 || sy >= h) {
            recycle(star, rng);
            continue;
        }

        int isx = static_cast<int>(sx);
        int isy = static_cast<int>(sy);

        // Size: larger when closer. A drawn star has z above the recycle bound, so the
        // range is scaled to that bound and rounds to span the full 1..3 px.
        int size = 1 + static_cast<int>(std::lround(2.0f * (1.0f - star.z) / Z_SPAN));

        // Brightness: brighter when closer, with minimum floor
        float bright_f = 80.0f + 175.0f * (1.0f - star.z);

        // Close stars show their color tint; distant stars stay white
        uint8_t r, g, b;
        if (star.z < COLOR_THRESHOLD) {
            float tint_mix = (COLOR_THRESHOLD - star.z) / COLOR_THRESHOLD;
            r = static_cast<uint8_t>(bright_f *
                                     (1.0f - tint_mix + tint_mix * star.tint_r / 255.0f));
            g = static_cast<uint8_t>(bright_f *
                                     (1.0f - tint_mix + tint_mix * star.tint_g / 255.0f));
            b = static_cast<uint8_t>(bright_f *
                                     (1.0f - tint_mix + tint_mix * star.tint_b / 255.0f));
        } else {
            r = g = b = static_cast<uint8_t>(bright_f);
        }

        star_dirty_[i].add(fill_square(writer, target, isx, isy, size, Rgb{r, g, b}));

        // Remember position for next step's erase pass
        star.prev_sx = static_cast<int16_t>(isx);
        star.prev_sy = static_cast<int16_t>(isy);
        star.prev_size = static_cast<uint8_t>(size);
    }

    for (const DirtyRect& r : star_dirty_) {
        if (!r.empty()) {
            dirty.push_back(r);
        }
    }
}

} // namespace helix::ui

#endif // HELIX_ENABLE_SCREENSAVER
