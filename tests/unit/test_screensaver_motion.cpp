// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screensaver_motion.h"

#ifdef HELIX_ENABLE_SCREENSAVER
#include "../lvgl_test_fixture.h"
#include "../test_helpers/screensaver_test_access.h"
#endif

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::ui::screensaver;

// ============================================================================
// MotionClock
// ============================================================================

TEST_CASE("MotionClock reports the time between calls", "[screensaver_motion]") {
    MotionClock clock;
    clock.reset(1000);
    CHECK(clock.advance(1033) == 33);
    CHECK(clock.advance(1050) == 17);
    CHECK(clock.advance(1050) == 0);
}

TEST_CASE("MotionClock measures across the tick wrap", "[screensaver_motion]") {
    MotionClock clock;
    clock.reset(UINT32_MAX - 10);
    // 10 ms up to UINT32_MAX, 1 ms to wrap to 0, 20 ms more.
    CHECK(clock.advance(20) == 31);
    CHECK(clock.advance(40) == 20);
}

TEST_CASE("MotionClock clamps one long gap", "[screensaver_motion]") {
    MotionClock clock;
    clock.reset(0);
    CHECK(clock.advance(MotionClock::MAX_GAP_MS) == MotionClock::MAX_GAP_MS);
    CHECK(clock.advance(MotionClock::MAX_GAP_MS + 60000) == MotionClock::MAX_GAP_MS);
    // The clamp spends the whole gap: the next call measures from the late call.
    CHECK(clock.advance(MotionClock::MAX_GAP_MS + 60010) == 10);
}

TEST_CASE("MotionClock reset restarts the measurement", "[screensaver_motion]") {
    MotionClock clock;
    clock.reset(0);
    CHECK(clock.advance(100) == 100);
    clock.reset(90000);
    CHECK(clock.advance(90025) == 25);
}

// ============================================================================
// flight_pos_at
// ============================================================================

TEST_CASE("flight_pos_at holds the start position until the delay ends", "[screensaver_motion]") {
    const FlightPos pos = flight_pos_at(4999, 700, -80, 10000, 5000, 1600);
    CHECK_FALSE(pos.started);
    CHECK(pos.x == 700);
    CHECK(pos.y == -80);
}

TEST_CASE("flight_pos_at starts at the start position when the delay ends",
          "[screensaver_motion]") {
    const FlightPos pos = flight_pos_at(5000, 700, -80, 10000, 5000, 1600);
    CHECK(pos.started);
    CHECK(pos.x == 700);
    CHECK(pos.y == -80);
}

TEST_CASE("flight_pos_at moves left and down in proportion to flight time",
          "[screensaver_motion]") {
    SECTION("no delay") {
        // 2500 of 10000 ms = a quarter of 1600 px.
        const FlightPos pos = flight_pos_at(2500, 700, -80, 10000, 0, 1600);
        CHECK(pos.started);
        CHECK(pos.x == 300);
        CHECK(pos.y == 320);
    }
    SECTION("measured from the end of the delay") {
        // 110 ms into a 24 s flight: 1600 * 110 / 24000 = 7.33, truncated to 7.
        const FlightPos pos = flight_pos_at(12110, 10, 20, 24000, 12000, 1600);
        CHECK(pos.started);
        CHECK(pos.x == 3);
        CHECK(pos.y == 27);
    }
}

TEST_CASE("flight_pos_at repeats the flight from the start", "[screensaver_motion]") {
    CHECK(flight_pos_at(11000, 700, -80, 10000, 1000, 1600).x == 700);
    CHECK(flight_pos_at(11000, 700, -80, 10000, 1000, 1600).y == -80);
    // 3 flights and 1 s later: 1600 / 10 = 160 px along.
    const FlightPos pos = flight_pos_at(1000 + 3 * 10000 + 1000, 700, -80, 10000, 1000, 1600);
    CHECK(pos.x == 540);
    CHECK(pos.y == 80);
}

TEST_CASE("flight_pos_at is exact at the end of a long 24 s flight", "[screensaver_motion]") {
    // Last millisecond of the flight: 1600 * 23999 / 24000 = 1599.93, truncated to 1599.
    const FlightPos pos = flight_pos_at(23999, 0, 0, 24000, 0, 1600);
    CHECK(pos.x == -1599);
    CHECK(pos.y == 1599);
}

// ============================================================================
// flap_frame_at
// ============================================================================

namespace {
std::vector<int> flap_sequence(int32_t delay_ms, uint32_t step_ms, uint8_t initial, int count) {
    std::vector<int> frames;
    for (int i = 0; i < count; i++) {
        frames.push_back(flap_frame_at(static_cast<uint32_t>(delay_ms) + i * step_ms, delay_ms,
                                       step_ms, initial));
    }
    return frames;
}
} // namespace

TEST_CASE("flap_frame_at ping-pongs through the four wing frames", "[screensaver_motion]") {
    CHECK(flap_sequence(0, 50, 0, 13) == std::vector<int>{0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1, 0});
}

TEST_CASE("flap_frame_at starts a reversed toaster at its initial frame", "[screensaver_motion]") {
    CHECK(flap_sequence(0, 50, 2, 8) == std::vector<int>{2, 3, 2, 1, 0, 1, 2, 3});
}

TEST_CASE("flap_frame_at holds each frame for one step", "[screensaver_motion]") {
    CHECK(flap_frame_at(0, 0, 100, 0) == 0);
    CHECK(flap_frame_at(99, 0, 100, 0) == 0);
    CHECK(flap_frame_at(100, 0, 100, 0) == 1);
    CHECK(flap_frame_at(299, 0, 100, 0) == 2);
    CHECK(flap_frame_at(300, 0, 100, 0) == 3);
}

TEST_CASE("flap_frame_at shows the initial frame until the delay ends", "[screensaver_motion]") {
    CHECK(flap_frame_at(0, 4000, 50, 2) == 2);
    CHECK(flap_frame_at(3999, 4000, 50, 2) == 2);
    CHECK(flap_sequence(4000, 50, 2, 3) == std::vector<int>{2, 3, 2});
}

// ============================================================================
// StepAccumulator
// ============================================================================

TEST_CASE("StepAccumulator carries a partial step", "[screensaver_motion]") {
    StepAccumulator acc;
    CHECK(acc.steps_due(60, 100, 3) == 0);
    CHECK(acc.steps_due(60, 100, 3) == 1); // 120: one step, 20 carried
    CHECK(acc.steps_due(79, 100, 3) == 0); // 99
    CHECK(acc.steps_due(1, 100, 3) == 1);  // 100
}

TEST_CASE("StepAccumulator gives one step per step length in a long gap", "[screensaver_motion]") {
    StepAccumulator acc;
    CHECK(acc.steps_due(300, 100, 3) == 3);
    CHECK(acc.steps_due(0, 100, 3) == 0);
}

TEST_CASE("StepAccumulator caps a call and drops the steps beyond the cap",
          "[screensaver_motion]") {
    StepAccumulator acc;
    // 4 steps due, 50 ms carried.
    CHECK(acc.steps_due(450, 100, 3) == 3);
    // The fourth step is not owed to the next call.
    CHECK(acc.steps_due(0, 100, 3) == 0);
    // The partial step still is.
    CHECK(acc.steps_due(50, 100, 3) == 1);
}

TEST_CASE("StepAccumulator reset drops the partial step", "[screensaver_motion]") {
    StepAccumulator acc;
    CHECK(acc.steps_due(90, 100, 3) == 0);
    acc.reset();
    CHECK(acc.steps_due(90, 100, 3) == 0);
    CHECK(acc.steps_due(10, 100, 3) == 1);
}

// ============================================================================
// Owned random numbers
// ============================================================================

TEST_CASE("unit_random stays within [0, 1] and spans it", "[screensaver_motion]") {
    std::minstd_rand rng(42);
    float lo = 1.0f;
    float hi = 0.0f;
    for (int i = 0; i < 10000; i++) {
        const float v = unit_random(rng);
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 1.0f);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    CHECK(lo < 0.01f);
    CHECK(hi > 0.99f);
}

TEST_CASE("random_below stays within [0, n) and reaches every value", "[screensaver_motion]") {
    std::minstd_rand rng(7);
    std::vector<int> seen(6, 0);
    for (int i = 0; i < 6000; i++) {
        const int v = random_below(rng, 6);
        REQUIRE(v >= 0);
        REQUIRE(v < 6);
        seen[static_cast<size_t>(v)]++;
    }
    for (int count : seen) {
        CHECK(count > 0);
    }
}

#ifdef HELIX_ENABLE_SCREENSAVER

// ============================================================================
// Starfield and pipes run on frame time
// ============================================================================

namespace {

using StarAccess = StarfieldScreensaverTestAccess;
using PipesAccess = PipesScreensaverTestAccess;

void run_timer(lv_timer_t* timer) {
    REQUIRE(timer != nullptr);
    REQUIRE(timer->timer_cb != nullptr);
    timer->timer_cb(timer);
}

} // namespace

TEST_CASE_METHOD(
    LVGLTestFixture,
    "StarfieldScreensaver level 0: configured period, else 16 ms, with the refresh equal",
    "[screensaver][screensaver_motion]") {
    const uint32_t configured_ms = GENERATE(as<uint32_t>{}, 0, 20);
    INFO("configured period " << configured_ms << " ms");
    const LevelZeroPeriods periods = level_zero_periods<StarfieldScreensaver>(configured_ms);
    CHECK(periods.timer_ms == (configured_ms != 0 ? configured_ms : helix::ui::SAVER_FAST_PERIOD));
    CHECK(periods.refresh_ms == periods.timer_ms);
}

TEST_CASE_METHOD(LVGLTestFixture, "StarfieldScreensaver moves stars in proportion to frame time",
                 "[screensaver][screensaver_motion]") {
    StarfieldScreensaver ss;
    ScreensaverStopOnExit<StarfieldScreensaver> stop_on_exit{ss};
    ss.start();
    REQUIRE(ss.is_active());

    // Near the centre and far from the camera, every star stays on screen for the whole
    // window, so none is recycled.
    StarAccess::place_stars(ss, 0.05f, 0.05f, 0.8f);
    const auto placed = StarAccess::stars(ss);
    REQUIRE_FALSE(placed.empty());

    lv_tick_inc(66);
    run_timer(SaverTestAccess::timer(ss));
    const auto after_66 = StarAccess::stars(ss);
    lv_tick_inc(11);
    run_timer(SaverTestAccess::timer(ss));
    const auto after_77 = StarAccess::stars(ss);

    for (size_t i = 0; i < placed.size(); i++) {
        CAPTURE(i);
        // A recycled star moves to a new random point, which would void the depth checks.
        REQUIRE(screensaver_same_bits(after_77[i].x, placed[i].x));
        REQUIRE(screensaver_same_bits(after_77[i].y, placed[i].y));
        // A star's speed is depth per 33 ms.
        CHECK(after_66[i].z == Catch::Approx(placed[i].z - placed[i].speed * 66.0f / 33.0f));
        CHECK(after_77[i].z == Catch::Approx(placed[i].z - placed[i].speed * 77.0f / 33.0f));
    }
}

TEST_CASE_METHOD(LVGLTestFixture, "StarfieldScreensaver with the same seed replays the same stars",
                 "[screensaver][screensaver_motion]") {
    StarfieldScreensaver a;
    ScreensaverStopOnExit<StarfieldScreensaver> stop_a{a};
    StarfieldScreensaver b;
    ScreensaverStopOnExit<StarfieldScreensaver> stop_b{b};
    StarfieldScreensaver other;
    ScreensaverStopOnExit<StarfieldScreensaver> stop_other{other};
    SaverTestAccess::set_fixed_seed(a, 1234);
    SaverTestAccess::set_fixed_seed(b, 1234);
    SaverTestAccess::set_fixed_seed(other, 99);

    a.start();
    b.start();
    other.start();
    REQUIRE(a.is_active());
    REQUIRE(b.is_active());
    REQUIRE(other.is_active());
    CHECK(StarAccess::stars(a) == StarAccess::stars(b));
    CHECK_FALSE(StarAccess::stars(a) == StarAccess::stars(other));

    // The two tick in turn, so a random sequence they shared would hand each a different
    // part of it whenever a star is recycled.
    int recycled = 0;
    for (int frame = 0; frame < 30; frame++) {
        lv_tick_inc(33);
        run_timer(SaverTestAccess::timer(a));
        run_timer(SaverTestAccess::timer(b));
        for (const auto& star : StarAccess::stars(a)) {
            if (screensaver_same_bits(star.z, 1.0f)) {
                recycled++;
            }
        }
    }
    REQUIRE(recycled > 0);
    CHECK(StarAccess::stars(a) == StarAccess::stars(b));
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "StarfieldScreensaver restarts its seed and clock on start after stop",
                 "[screensaver][screensaver_motion]") {
    StarfieldScreensaver ss;
    ScreensaverStopOnExit<StarfieldScreensaver> stop_on_exit{ss};
    SaverTestAccess::set_fixed_seed(ss, 4321);

    ss.start();
    REQUIRE(ss.is_active());
    const auto at_start = StarAccess::stars(ss);
    lv_tick_inc(66);
    run_timer(SaverTestAccess::timer(ss));
    const auto first_frame = StarAccess::stars(ss);

    int moved = 0;
    for (size_t i = 0; i < at_start.size(); i++) {
        // A recycled star has a new random position.
        if (!screensaver_same_bits(first_frame[i].x, at_start[i].x)) {
            continue;
        }
        CAPTURE(i);
        CHECK(first_frame[i].z == Catch::Approx(at_start[i].z - at_start[i].speed * 2.0f));
        moved++;
    }
    REQUIRE(moved > 0);

    ss.stop();
    lv_tick_inc(5000);
    ss.start();
    REQUIRE(ss.is_active());
    CHECK(StarAccess::stars(ss) == at_start);

    lv_tick_inc(66);
    run_timer(SaverTestAccess::timer(ss));
    CHECK(StarAccess::stars(ss) == first_frame);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "PipesScreensaver level 0: configured period, else 16 ms, with the refresh equal",
                 "[screensaver][screensaver_motion]") {
    const uint32_t configured_ms = GENERATE(as<uint32_t>{}, 0, 20);
    INFO("configured period " << configured_ms << " ms");
    const LevelZeroPeriods periods = level_zero_periods<PipesScreensaver>(configured_ms);
    CHECK(periods.timer_ms == (configured_ms != 0 ? configured_ms : helix::ui::SAVER_FAST_PERIOD));
    CHECK(periods.refresh_ms == periods.timer_ms);
}

TEST_CASE_METHOD(LVGLTestFixture, "PipesScreensaver grows one step per 100 ms of frame time",
                 "[screensaver][screensaver_motion]") {
    PipesScreensaver one_call;
    ScreensaverStopOnExit<PipesScreensaver> stop_one{one_call};
    PipesScreensaver per_step;
    ScreensaverStopOnExit<PipesScreensaver> stop_per_step{per_step};
    SaverTestAccess::set_fixed_seed(one_call, 77);
    SaverTestAccess::set_fixed_seed(per_step, 77);

    one_call.start();
    per_step.start();
    REQUIRE(one_call.is_active());
    REQUIRE(per_step.is_active());
    REQUIRE(PipesAccess::same_scene(one_call, per_step));

    for (int i = 0; i < 3; i++) {
        lv_tick_inc(100);
        run_timer(SaverTestAccess::timer(per_step));
    }
    run_timer(SaverTestAccess::timer(one_call));

    // A 300 ms gap grows each of the two starting pipes three times.
    const auto pipes = PipesAccess::pipes(one_call);
    CHECK(pipes[0].segment_count == 3);
    CHECK(pipes[1].segment_count == 3);
    CHECK(PipesAccess::same_scene(one_call, per_step));
}

TEST_CASE_METHOD(LVGLTestFixture, "PipesScreensaver caps the steps one callback draws",
                 "[screensaver][screensaver_motion]") {
    PipesScreensaver ss;
    ScreensaverStopOnExit<PipesScreensaver> stop_on_exit{ss};
    SaverTestAccess::set_fixed_seed(ss, 5);
    ss.start();
    REQUIRE(ss.is_active());

    // Four steps are due; three are drawn.
    lv_tick_inc(450);
    run_timer(SaverTestAccess::timer(ss));
    CHECK(PipesAccess::pipes(ss)[0].segment_count == 3);
    CHECK(PipesAccess::pipes(ss)[1].segment_count == 3);

    // The fourth is dropped rather than owed to the next callback.
    run_timer(SaverTestAccess::timer(ss));
    CHECK(PipesAccess::pipes(ss)[0].segment_count == 3);

    // The 50 ms left over still counts toward the next step.
    lv_tick_inc(50);
    run_timer(SaverTestAccess::timer(ss));
    CHECK(PipesAccess::pipes(ss)[0].segment_count == 4);
}

TEST_CASE_METHOD(LVGLTestFixture, "PipesScreensaver drops the steps still due when the grid resets",
                 "[screensaver][screensaver_motion]") {
    PipesScreensaver ss;
    ScreensaverStopOnExit<PipesScreensaver> stop_on_exit{ss};
    SaverTestAccess::set_fixed_seed(ss, 11);
    ss.start();
    REQUIRE(ss.is_active());
    PipesAccess::set_total_segments(ss, PipesAccess::max_segments() + 1);

    // 3.5 steps are due: the first resets the full grid, and the rest belong to the old scene.
    lv_tick_inc(350);
    run_timer(SaverTestAccess::timer(ss));
    REQUIRE(PipesAccess::total_segments(ss) == 0);
    for (const auto& pipe : PipesAccess::pipes(ss)) {
        CHECK(pipe.segment_count == 0);
    }

    // The new scene's first step comes a full step after the reset.
    lv_tick_inc(60);
    run_timer(SaverTestAccess::timer(ss));
    CHECK(PipesAccess::total_segments(ss) == 0);
    lv_tick_inc(40);
    run_timer(SaverTestAccess::timer(ss));
    CHECK(PipesAccess::pipes(ss)[0].segment_count == 1);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "PipesScreensaver restarts its seed and step clock on start after stop",
                 "[screensaver][screensaver_motion]") {
    PipesScreensaver ss;
    ScreensaverStopOnExit<PipesScreensaver> stop_on_exit{ss};
    SaverTestAccess::set_fixed_seed(ss, 3);

    ss.start();
    REQUIRE(ss.is_active());
    const auto at_start = PipesAccess::pipes(ss);
    lv_tick_inc(250);
    run_timer(SaverTestAccess::timer(ss));
    REQUIRE(PipesAccess::pipes(ss)[0].segment_count == 2);

    ss.stop();
    lv_tick_inc(5000);
    ss.start();
    REQUIRE(ss.is_active());
    // The seed replays the two starting pipes. A dead pipe's fields are never read.
    CHECK(PipesAccess::pipes(ss)[0] == at_start[0]);
    CHECK(PipesAccess::pipes(ss)[1] == at_start[1]);

    // Neither the 50 ms left from the first run nor the time spent stopped counts.
    lv_tick_inc(60);
    run_timer(SaverTestAccess::timer(ss));
    CHECK(PipesAccess::total_segments(ss) == 0);
    lv_tick_inc(40);
    run_timer(SaverTestAccess::timer(ss));
    CHECK(PipesAccess::pipes(ss)[0].segment_count == 1);
}

TEST_CASE_METHOD(LVGLTestFixture, "PipesScreensaver with the same seed replays the same scene",
                 "[screensaver][screensaver_motion]") {
    PipesScreensaver a;
    ScreensaverStopOnExit<PipesScreensaver> stop_a{a};
    PipesScreensaver b;
    ScreensaverStopOnExit<PipesScreensaver> stop_b{b};
    PipesScreensaver other;
    ScreensaverStopOnExit<PipesScreensaver> stop_other{other};
    SaverTestAccess::set_fixed_seed(a, 2024);
    SaverTestAccess::set_fixed_seed(b, 2024);
    SaverTestAccess::set_fixed_seed(other, 7);

    a.start();
    b.start();
    other.start();
    REQUIRE(a.is_active());
    REQUIRE(b.is_active());
    REQUIRE(other.is_active());
    CHECK(PipesAccess::same_scene(a, b));
    CHECK_FALSE(PipesAccess::same_scene(a, other));

    // The two grow in turn, so a random sequence they shared would steer them apart.
    for (int i = 0; i < 20; i++) {
        lv_tick_inc(100);
        run_timer(SaverTestAccess::timer(a));
        run_timer(SaverTestAccess::timer(b));
    }
    REQUIRE(PipesAccess::total_segments(a) > 20);
    CHECK(PipesAccess::same_scene(a, b));
}

#endif // HELIX_ENABLE_SCREENSAVER
