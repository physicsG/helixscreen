// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_ENABLE_SCREENSAVER

#include "../lvgl_test_fixture.h"
#include "../test_helpers/screensaver_sim_kit.h"
#include "../test_helpers/screensaver_test_access.h"
#include "screensaver_pixel_writer.h"
#include "screensaver_starfield_sim.h"

#include <algorithm>
#include <random>
#include <utility>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::ui::DirtyRect;
using helix::ui::FrameTarget;
using helix::ui::StarfieldSim;
using StarAccess = StarfieldScreensaverTestAccess;

namespace {

constexpr uint32_t kFrameW = 320;
constexpr uint32_t kFrameH = 200;

/// A frame painted opaque black, as the canvas hands one to a saver.
struct Frame : helix::test::TestFrame {
    Frame() : TestFrame(kFrameW, kFrameH, helix::ui::PixelFormat::XRGB8888) {
        helix::ui::PixelWriter(target).fill(helix::ui::Rgb{0, 0, 0});
    }
};

} // namespace

// ============================================================================
// StarfieldSim: one frame of stars
// ============================================================================

TEST_CASE("StarfieldSim dirties only around stars near the centre",
          "[screensaver][starfield_sim]") {
    std::minstd_rand rng(11);
    StarfieldSim sim;
    Frame frame;
    sim.init(frame.target, rng, 0);
    for (auto& star : sim.stars()) {
        star.x = 0.02f;
        star.y = -0.02f;
        star.z = 0.9f;
    }
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
    CHECK(area < static_cast<int64_t>(kFrameW) * kFrameH / 100);
}

TEST_CASE("init places a full population whatever count the last run flew",
          "[screensaver][starfield_sim]") {
    std::minstd_rand rng(11);
    StarfieldSim sim;
    Frame frame;
    sim.init(frame.target, rng, 0);
    sim.set_active_count(32);
    REQUIRE(sim.active_count() == 32);

    // A saver re-initialises on every start, and the level it last ran at must not
    // survive into the new run.
    sim.init(frame.target, rng, 0);
    CHECK(sim.active_count() == StarfieldSim::kNumStars);

    std::vector<DirtyRect> dirty;
    sim.step(33, frame.target, rng, dirty);
    CHECK(dirty.size() > 32);
}

TEST_CASE("a scattered sky reports one small rect per star", "[screensaver][starfield_sim]") {
    std::minstd_rand rng(23);
    StarfieldSim sim;
    Frame frame;
    sim.init(frame.target, rng, 0);
    std::vector<DirtyRect> dirty;
    for (int step = 0; step < 60; step++) {
        sim.step(33, frame.target, rng, dirty);
    }

    // A spread population pushes many rects, each clipped to the frame, whose summed
    // area is a small fraction of it - not one box spanning the screen.
    REQUIRE(dirty.size() > 32);
    CHECK(dirty.size() <= helix::ui::kSaverMaxDirtyAreas);
    int64_t covered = 0;
    for (const DirtyRect& r : dirty) {
        CAPTURE(r.x1, r.y1, r.x2, r.y2);
        CHECK(r.x1 >= 0);
        CHECK(r.y1 >= 0);
        CHECK(r.x2 < static_cast<int32_t>(kFrameW));
        CHECK(r.y2 < static_cast<int32_t>(kFrameH));
        covered += r.area();
    }
    CHECK(covered * 20 < static_cast<int64_t>(kFrameW) * kFrameH);
}

TEST_CASE("a star's size spans the full 1..3 pixels of its depth range",
          "[screensaver][starfield_sim]") {
    std::minstd_rand rng(3);
    StarfieldSim sim;
    Frame frame;
    sim.init(frame.target, rng, 0);
    // Parked dead centre and moving nowhere, so each one draws every step.
    for (auto& star : sim.stars()) {
        star.x = 0.0f;
        star.y = 0.0f;
        star.speed = 0.0f;
    }
    sim.stars()[0].z = 0.02f; // just above the recycle bound: the closest a star draws
    sim.stars()[1].z = 0.5f;
    sim.stars()[2].z = 0.9f; // far away
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
    Frame frame;
    sim.init(frame.target, rng, 0);
    // Parked dead centre and moving nowhere, so every active star draws every step.
    for (auto& star : sim.stars()) {
        star.x = 0.0f;
        star.y = 0.0f;
        star.z = 0.5f;
        star.speed = 0.0f;
    }
    std::vector<DirtyRect> dirty;

    for (const int count : {StarfieldSim::kNumStars, 96, 64, 32}) {
        CAPTURE(count);
        sim.set_active_count(count);
        sim.step(33, frame.target, rng, dirty); // stars parked by the change erase here
        sim.step(33, frame.target, rng, dirty);
        CHECK(sim.active_count() == count);
        CHECK(dirty.size() == static_cast<size_t>(count));
        CHECK(dirty.size() <= helix::ui::kSaverMaxDirtyAreas);
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
    advance_saver(ss, 33);

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
        advance_saver(ss, 33);
    };

    step_frame();
    // One area per star, never the single spanning box a scattered sky would force.
    CHECK(invalidated.areas.size() == 150);
    CHECK(invalidated.areas.size() <= helix::ui::kSaverMaxDirtyAreas);

    const int level_stars[] = {150, 96, 64, 32};
    for (size_t level = 1; level < ss.level_count(); level++) {
        CAPTURE(level);
        ss.request_level(level);
        CHECK(StarAccess::active_star_count(ss) == level_stars[level]);
        step_frame(); // the stars the rung parks erase here
        step_frame();
        CHECK(invalidated.areas.size() == static_cast<size_t>(level_stars[level]));
        CHECK(invalidated.areas.size() <= helix::ui::kSaverMaxDirtyAreas);
    }
    CHECK(ss.level() == 3);
}

#endif // HELIX_ENABLE_SCREENSAVER
