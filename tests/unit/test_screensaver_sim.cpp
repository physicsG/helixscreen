// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_ENABLE_SCREENSAVER

#include "../lvgl_test_fixture.h"
#include "../test_helpers/screensaver_test_access.h"
#include "screensaver_pixel_writer.h"
#include "screensaver_starfield_sim.h"

#include <algorithm>
#include <cstring>
#include <random>
#include <utility>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::ui::DirtyRect;
using helix::ui::FrameTarget;
using helix::ui::StarfieldSim;
using StarAccess = StarfieldScreensaverTestAccess;

namespace {

constexpr uint32_t FRAME_W = 320;
constexpr uint32_t FRAME_H = 200;
// Wider than 4 bytes per pixel, as LVGL's aligned strides can be, so a write indexed by
// width instead of stride lands on the wrong row.
constexpr uint32_t FRAME_STRIDE = FRAME_W * 4 + 12;

/// A heap frame painted opaque black.
struct Frame {
    std::vector<uint8_t> bytes;
    FrameTarget target;

    Frame() : bytes(static_cast<size_t>(FRAME_STRIDE) * FRAME_H, 0) {
        target = {bytes.data(), FRAME_STRIDE, FRAME_W, FRAME_H};
        helix::ui::PixelWriter(target).fill(helix::ui::Rgb{0, 0, 0});
    }
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
};

bool rect_contains(const DirtyRect& r, uint32_t x, uint32_t y) {
    const auto px = static_cast<int32_t>(x);
    const auto py = static_cast<int32_t>(y);
    return !r.empty() && px >= r.x1 && px <= r.x2 && py >= r.y1 && py <= r.y2;
}

struct Diff {
    size_t changed = 0;
    size_t outside = 0;
};

/// Counts the pixels of `after` that differ from `before`, and those of them outside every
/// box in `rects`.
Diff diff_frames(const std::vector<uint8_t>& before, const FrameTarget& after,
                 const std::vector<DirtyRect>& rects) {
    Diff diff;
    for (uint32_t y = 0; y < after.h; y++) {
        for (uint32_t x = 0; x < after.w; x++) {
            const size_t i = static_cast<size_t>(y) * after.stride + x * 4;
            if (std::memcmp(&before[i], after.data + i, 4) == 0) {
                continue;
            }
            diff.changed++;
            const bool covered =
                std::any_of(rects.begin(), rects.end(),
                            [x, y](const DirtyRect& r) { return rect_contains(r, x, y); });
            diff.outside += covered ? 0 : 1;
        }
    }
    return diff;
}

/// Pixels with any colour.
size_t lit_pixels(const FrameTarget& f) {
    size_t count = 0;
    for (uint32_t y = 0; y < f.h; y++) {
        const uint8_t* row = f.data + static_cast<size_t>(y) * f.stride;
        for (uint32_t x = 0; x < f.w; x++) {
            count += (row[x * 4] | row[x * 4 + 1] | row[x * 4 + 2]) != 0 ? 1 : 0;
        }
    }
    return count;
}

/// Pixels whose X byte is not 0xFF.
size_t non_opaque_pixels(const FrameTarget& f) {
    size_t count = 0;
    for (uint32_t y = 0; y < f.h; y++) {
        const uint8_t* row = f.data + static_cast<size_t>(y) * f.stride;
        for (uint32_t x = 0; x < f.w; x++) {
            count += row[x * 4 + 3] != 0xFF ? 1 : 0;
        }
    }
    return count;
}

void run_timer(lv_timer_t* timer) {
    REQUIRE(timer != nullptr);
    REQUIRE(timer->timer_cb != nullptr);
    timer->timer_cb(timer);
}

} // namespace

// ============================================================================
// StarfieldSim: one frame of stars
// ============================================================================

TEST_CASE("StarfieldSim dirties every pixel its step changes", "[screensaver][starfield_sim]") {
    std::minstd_rand rng(7);
    StarfieldSim sim;
    sim.init(FRAME_W, FRAME_H, rng);
    Frame frame;

    size_t changed = 0;
    for (int step = 0; step < 90; step++) {
        CAPTURE(step);
        const std::vector<uint8_t> before = frame.bytes;
        std::vector<DirtyRect> dirty;
        sim.step(33, frame.target, rng, dirty);
        const Diff diff = diff_frames(before, frame.target, dirty);
        CHECK(diff.outside == 0);
        changed += diff.changed;
    }
    REQUIRE(changed > 0);
}

TEST_CASE("StarfieldSim dirties only around stars near the centre",
          "[screensaver][starfield_sim]") {
    std::minstd_rand rng(11);
    StarfieldSim sim;
    sim.init(FRAME_W, FRAME_H, rng);
    for (auto& star : sim.stars()) {
        star.x = 0.02f;
        star.y = -0.02f;
        star.z = 0.9f;
    }
    Frame frame;
    std::vector<DirtyRect> dirty;
    sim.step(33, frame.target, rng, dirty);
    REQUIRE_FALSE(dirty.empty());

    // Erases the stars and draws them again a pixel or two away.
    sim.step(33, frame.target, rng, dirty);
    REQUIRE_FALSE(dirty.empty());
    DirtyRect span;
    for (const DirtyRect& r : dirty) {
        span.add(r);
    }
    REQUIRE_FALSE(span.empty());
    const int64_t area = static_cast<int64_t>(span.x2 - span.x1 + 1) * (span.y2 - span.y1 + 1);
    CHECK(area < static_cast<int64_t>(FRAME_W) * FRAME_H / 100);
}

TEST_CASE("StarfieldSim replays exactly from a seed", "[screensaver][starfield_sim]") {
    const auto run = [](uint32_t seed) {
        std::minstd_rand rng(seed);
        StarfieldSim sim;
        sim.init(FRAME_W, FRAME_H, rng);
        Frame frame;
        std::vector<DirtyRect> dirty;
        std::vector<DirtyRect> step_dirty;
        for (int i = 0; i < 60; i++) {
            for (uint32_t dt : {33u, 16u, 50u, 7u, 70u}) {
                sim.step(dt, frame.target, rng, step_dirty);
                dirty.insert(dirty.end(), step_dirty.begin(), step_dirty.end());
            }
        }
        return std::make_pair(frame.bytes, dirty);
    };

    const auto first = run(42);
    const Frame black;
    REQUIRE(first.first != black.bytes);
    CHECK(run(42) == first);
    CHECK_FALSE(run(43).first == first.first);
}

TEST_CASE("every X byte of a StarfieldSim frame stays 0xFF as stars move and recycle",
          "[screensaver][starfield_sim]") {
    std::minstd_rand rng(5);
    StarfieldSim sim;
    sim.init(FRAME_W, FRAME_H, rng);
    Frame frame;
    REQUIRE(non_opaque_pixels(frame.target) == 0);

    size_t most_lit = 0;
    std::vector<DirtyRect> dirty;
    for (int step = 0; step < 120; step++) {
        CAPTURE(step);
        sim.step(33, frame.target, rng, dirty);
        most_lit = std::max(most_lit, lit_pixels(frame.target));
        CHECK(non_opaque_pixels(frame.target) == 0);
    }
    // The check covered drawn stars, not only the black background.
    REQUIRE(most_lit > 0);
}

TEST_CASE("init places a full population whatever count the last run flew",
          "[screensaver][starfield_sim]") {
    std::minstd_rand rng(11);
    StarfieldSim sim;
    sim.init(FRAME_W, FRAME_H, rng);
    sim.set_active_count(32);
    REQUIRE(sim.active_count() == 32);

    // A saver re-initialises on every start, and the level it last ran at must not
    // survive into the new run.
    sim.init(FRAME_W, FRAME_H, rng);
    CHECK(sim.active_count() == StarfieldSim::NUM_STARS);

    Frame frame;
    std::vector<DirtyRect> dirty;
    sim.step(33, frame.target, rng, dirty);
    CHECK(dirty.size() > 32);
}

TEST_CASE("a scattered sky reports one small rect per star", "[screensaver][starfield_sim]") {
    std::minstd_rand rng(23);
    StarfieldSim sim;
    sim.init(FRAME_W, FRAME_H, rng);
    Frame frame;
    std::vector<DirtyRect> dirty;
    for (int step = 0; step < 60; step++) {
        sim.step(33, frame.target, rng, dirty);
    }

    // A spread population pushes many rects, each clipped to the frame, whose summed
    // area is a small fraction of it - not one box spanning the screen.
    REQUIRE(dirty.size() > 32);
    CHECK(dirty.size() <= helix::ui::SAVER_MAX_DIRTY_AREAS);
    int64_t covered = 0;
    for (const DirtyRect& r : dirty) {
        CAPTURE(r.x1, r.y1, r.x2, r.y2);
        CHECK(r.x1 >= 0);
        CHECK(r.y1 >= 0);
        CHECK(r.x2 < static_cast<int32_t>(FRAME_W));
        CHECK(r.y2 < static_cast<int32_t>(FRAME_H));
        covered += r.area();
    }
    CHECK(covered * 20 < static_cast<int64_t>(FRAME_W) * FRAME_H);
}

TEST_CASE("a star's size spans the full 1..3 pixels of its depth range",
          "[screensaver][starfield_sim]") {
    std::minstd_rand rng(3);
    StarfieldSim sim;
    sim.init(FRAME_W, FRAME_H, rng);
    // Parked dead centre and moving nowhere, so each one draws every step.
    for (auto& star : sim.stars()) {
        star.x = 0.0f;
        star.y = 0.0f;
        star.speed = 0.0f;
    }
    sim.stars()[0].z = 0.02f; // just above the recycle bound: the closest a star draws
    sim.stars()[1].z = 0.5f;
    sim.stars()[2].z = 0.9f; // far away
    Frame frame;
    std::vector<DirtyRect> dirty;

    sim.step(33, frame.target, rng, dirty);

    REQUIRE(sim.stars()[0].prev_size == 3);
    REQUIRE(sim.stars()[1].prev_size == 2);
    REQUIRE(sim.stars()[2].prev_size == 1);
    REQUIRE(dirty.size() == sim.stars().size());
    CHECK(dirty[0].area() == 9);
}

TEST_CASE("the active star count sets how many rects a frame pushes",
          "[screensaver][starfield_sim]") {
    std::minstd_rand rng(31);
    StarfieldSim sim;
    sim.init(FRAME_W, FRAME_H, rng);
    // Parked dead centre and moving nowhere, so every active star draws every step.
    for (auto& star : sim.stars()) {
        star.x = 0.0f;
        star.y = 0.0f;
        star.z = 0.5f;
        star.speed = 0.0f;
    }
    Frame frame;
    std::vector<DirtyRect> dirty;

    for (const int count : {StarfieldSim::NUM_STARS, 96, 64, 32}) {
        CAPTURE(count);
        sim.set_active_count(count);
        sim.step(33, frame.target, rng, dirty); // stars parked by the change erase here
        sim.step(33, frame.target, rng, dirty);
        CHECK(sim.active_count() == count);
        CHECK(dirty.size() == static_cast<size_t>(count));
        CHECK(dirty.size() <= helix::ui::SAVER_MAX_DIRTY_AREAS);
    }

    sim.set_active_count(0);
    CHECK(sim.active_count() == 1); // clamped: a level never empties the sky
}

// ============================================================================
// StarfieldScreensaver: stepping and invalidating in the timer callback
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "a starfield frame invalidates only what its step changed",
                 "[screensaver][starfield_sim]") {
    StarfieldScreensaver ss;
    ScreensaverStopOnExit<StarfieldScreensaver> stop_on_exit{ss};
    ss.start();
    REQUIRE(ss.is_active());
    // Stars near the centre change a few pixels, far less than the whole canvas.
    StarAccess::place_stars(ss, 0.02f, -0.02f, 0.9f);
    lv_obj_t* canvas_obj = SaverTestAccess::canvas(ss);
    lv_obj_update_layout(canvas_obj);
    lv_area_t coords;
    lv_obj_get_coords(canvas_obj, &coords);
    const int64_t canvas_px = lv_area_get_size(&coords);

    helix::test::InvalidatedAreas invalidated(lv_obj_get_display(canvas_obj));
    lv_tick_inc(33);
    run_timer(SaverTestAccess::timer(ss));

    // The frame erases and redraws the star cluster, so it invalidates, and every area
    // it invalidates is a fraction of the canvas.
    REQUIRE_FALSE(invalidated.areas.empty());
    for (const lv_area_t& a : invalidated.areas) {
        CHECK(lv_area_get_size(&a) < canvas_px);
    }
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "a starfield level thins the stars and the areas a frame invalidates",
                 "[screensaver][starfield_sim]") {
    StarfieldScreensaver ss;
    ScreensaverStopOnExit<StarfieldScreensaver> stop_on_exit{ss};
    ss.start();
    REQUIRE(ss.is_active());
    REQUIRE(ss.level_count() == 4);
    REQUIRE(StarAccess::active_star_count(ss) == 150);
    lv_obj_t* canvas_obj = SaverTestAccess::canvas(ss);
    // Invalidations are clipped to the object's coords, which a layout pass computes.
    lv_obj_update_layout(canvas_obj);
    helix::test::InvalidatedAreas invalidated(lv_obj_get_display(canvas_obj));

    // Stars drawn on frame at a fixed depth, so a frame's area count is its star count.
    StarAccess::place_stars(ss, 0.02f, -0.02f, 0.9f);
    const auto step_frame = [&]() {
        invalidated.areas.clear();
        lv_tick_inc(33);
        run_timer(SaverTestAccess::timer(ss));
    };

    step_frame();
    // One area per star, never the single spanning box a scattered sky would force.
    CHECK(invalidated.areas.size() == 150);
    CHECK(invalidated.areas.size() <= helix::ui::SAVER_MAX_DIRTY_AREAS);

    const int level_stars[] = {150, 96, 64, 32};
    for (size_t level = 1; level < ss.level_count(); level++) {
        CAPTURE(level);
        ss.request_level(level);
        CHECK(StarAccess::active_star_count(ss) == level_stars[level]);
        step_frame(); // the stars the rung parks erase here
        step_frame();
        CHECK(invalidated.areas.size() == static_cast<size_t>(level_stars[level]));
        CHECK(invalidated.areas.size() <= helix::ui::SAVER_MAX_DIRTY_AREAS);
    }
    CHECK(ss.level() == 3);
}

#endif // HELIX_ENABLE_SCREENSAVER
