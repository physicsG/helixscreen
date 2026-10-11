// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_ENABLE_SCREENSAVER

#include "../lvgl_test_fixture.h"
#include "../test_helpers/scoped_env.h"
#include "../test_helpers/screensaver_manager_test_access.h"
#include "../test_helpers/screensaver_test_access.h"
#include "config.h"
#include "screensaver.h"
#include "screensaver_canvas.h"
#include "screensaver_fireworks.h"
#include "screensaver_fireworks_sim.h"
#include "screensaver_pixel_writer.h"
#include "screensaver_registry.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <random>
#include <utility>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::ui::DirtyRect;
using helix::ui::FIREWORKS_LEVEL_COUNT;
using helix::ui::FIREWORKS_LEVELS;
using helix::ui::FireworksPacing;
using helix::ui::FireworksScreensaver;
using helix::ui::FireworksSim;
using helix::ui::FrameTarget;
using helix::ui::PixelFormat;
using helix::ui::PixelWriter;
using helix::ui::Rgb;

namespace {

constexpr uint32_t W = 320;
constexpr uint32_t H = 200;

uint32_t bytes_per_pixel(PixelFormat format) {
    return format == PixelFormat::RGB565 ? 2 : 4;
}

/// A heap frame whose rows are wider than w * bytes per pixel, as LVGL's aligned strides can be.
struct SimFrame {
    std::vector<uint8_t> bytes;
    FrameTarget target;

    explicit SimFrame(PixelFormat format) {
        const uint32_t bpp = bytes_per_pixel(format);
        const uint32_t stride = W * bpp + 3 * bpp;
        bytes.assign(static_cast<size_t>(stride) * H, 0);
        target = {bytes.data(), stride, W, H, format};
    }
    SimFrame(const SimFrame&) = delete;
    SimFrame& operator=(const SimFrame&) = delete;
};

/// A busy sky: a shell every 150 to 400 ms and a finale every 6 s.
FireworksPacing busy_pacing() {
    FireworksPacing pacing;
    pacing.first_launch_ms = 50;
    pacing.min_gap_ms = 150;
    pacing.max_gap_ms = 400;
    pacing.finale_every_ms = 6000;
    pacing.finale_span_ms = 1500;
    return pacing;
}

bool box_covers(const std::vector<DirtyRect>& boxes, int32_t x, int32_t y) {
    return std::any_of(boxes.begin(), boxes.end(), [&](const DirtyRect& b) {
        return x >= b.x1 && x <= b.x2 && y >= b.y1 && y <= b.y2;
    });
}

struct Diff {
    size_t changed = 0;
    size_t uncovered = 0;
};

/// Pixels of `frame` that differ from `before`, and how many of them no box in `boxes` covers.
Diff diff_frame(const std::vector<uint8_t>& before, const SimFrame& frame,
                const std::vector<DirtyRect>& boxes) {
    Diff diff;
    const uint32_t bpp = bytes_per_pixel(frame.target.format);
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            const size_t i = static_cast<size_t>(y) * frame.target.stride + x * bpp;
            if (std::memcmp(&before[i], &frame.bytes[i], bpp) == 0) {
                continue;
            }
            diff.changed++;
            diff.uncovered +=
                box_covers(boxes, static_cast<int32_t>(x), static_cast<int32_t>(y)) ? 0 : 1;
        }
    }
    return diff;
}

/// Pixels brighter than any sky or star colour: a spark or a rocket.
size_t lit_pixels(const SimFrame& frame) {
    const PixelWriter writer(frame.target);
    size_t count = 0;
    for (int32_t y = 0; y < static_cast<int32_t>(H); y++) {
        for (int32_t x = 0; x < static_cast<int32_t>(W); x++) {
            const Rgb c = writer.get(x, y);
            count += (c.r > 120 || c.g > 120) ? 1 : 0;
        }
    }
    return count;
}

int brightness(Rgb c) {
    return c.r + c.g + c.b;
}

void fire(lv_timer_t* timer) {
    REQUIRE(timer != nullptr);
    REQUIRE(timer->timer_cb != nullptr);
    timer->timer_cb(timer);
}

} // namespace

// ============================================================================
// FireworksSim
// ============================================================================

TEST_CASE("the fireworks sky is a gradient with faint stars over rolling hills, in either format",
          "[screensaver][fireworks_sim]") {
    const PixelFormat format = GENERATE(PixelFormat::XRGB8888, PixelFormat::RGB565);
    CAPTURE(static_cast<int>(format));
    std::minstd_rand rng(3);
    FireworksSim sim;
    SimFrame frame(format);
    sim.init(frame.target, rng, 0);

    // The frame holds exactly the computed sky, dithered as a redraw dithers it.
    SimFrame expected(format);
    PixelWriter painter(expected.target);
    for (int32_t y = 0; y < static_cast<int32_t>(H); y++) {
        for (int32_t x = 0; x < static_cast<int32_t>(W); x++) {
            painter.put_dithered(x, y, sim.sky_at(x, y));
        }
    }
    CHECK(frame.bytes == expected.bytes);

    const Rgb ground = sim.sky_at(0, static_cast<int32_t>(H) - 1);
    int32_t lowest_top = static_cast<int32_t>(H);
    int32_t highest_top = 0;
    size_t stars = 0;
    int32_t starless_column = -1;
    int32_t starless_top = 0;
    for (int32_t x = 0; x < static_cast<int32_t>(W); x++) {
        int32_t top = static_cast<int32_t>(H) - 1;
        while (top > 0 && sim.sky_at(x, top - 1) == ground) {
            top--;
        }
        lowest_top = std::min(lowest_top, top);
        highest_top = std::max(highest_top, top);
        size_t column_stars = 0;
        for (int32_t y = 0; y < top; y++) {
            column_stars += sim.sky_at(x, y).r >= 40 ? 1 : 0;
        }
        stars += column_stars;
        if (column_stars == 0 && starless_column < 0) {
            starless_column = x;
            starless_top = top;
        }
    }
    CHECK(highest_top > lowest_top);                 // rolling, not flat
    CHECK(lowest_top > static_cast<int32_t>(H) / 2); // the hills stay low
    CHECK(stars > 0);
    CHECK(stars <= static_cast<size_t>(FireworksSim::STAR_COUNT));
    REQUIRE(starless_column >= 0);
    // Darker at the top of the sky than just above the hills.
    CHECK(brightness(sim.sky_at(starless_column, 0)) <
          brightness(sim.sky_at(starless_column, starless_top - 1)));

    if (format == PixelFormat::XRGB8888) {
        size_t not_opaque = 0;
        for (uint32_t y = 0; y < H; y++) {
            for (uint32_t x = 0; x < W; x++) {
                not_opaque +=
                    frame.bytes[static_cast<size_t>(y) * frame.target.stride + x * 4 + 3] != 0xFF
                        ? 1
                        : 0;
            }
        }
        CHECK(not_opaque == 0);
    }
}

TEST_CASE("a fireworks show replays exactly from a seed, and another seed differs",
          "[screensaver][fireworks_sim]") {
    const PixelFormat format = GENERATE(PixelFormat::XRGB8888, PixelFormat::RGB565);
    CAPTURE(static_cast<int>(format));
    const auto run = [format](uint32_t seed) {
        std::minstd_rand rng(seed);
        FireworksSim sim;
        SimFrame frame(format);
        sim.init(frame.target, rng, 0, busy_pacing());
        std::vector<std::vector<DirtyRect>> boxes;
        std::vector<DirtyRect> dirty;
        constexpr uint32_t FRAME_MS[] = {16, 33, 50, 7, 100};
        for (int i = 0; i < 400; i++) {
            sim.step(FRAME_MS[i % 5], frame.target, rng, dirty);
            boxes.push_back(dirty);
        }
        return std::make_pair(frame.bytes, boxes);
    };

    // Same seed twice, then a different one. Naming all three before comparing keeps the
    // assertion about the VALUES rather than about a call made inside the CHECK.
    const auto first = run(9);
    const auto repeat = run(9);
    const auto other = run(10);

    CHECK(repeat == first);
    CHECK_FALSE(other.first == first.first);
}

TEST_CASE("every pixel a fireworks frame changes lies in one of its dirty boxes",
          "[screensaver][fireworks_sim]") {
    const PixelFormat format = GENERATE(PixelFormat::XRGB8888, PixelFormat::RGB565);
    CAPTURE(static_cast<int>(format));
    std::minstd_rand rng(21);
    FireworksSim sim;
    SimFrame frame(format);
    sim.init(frame.target, rng, 0, busy_pacing());

    std::vector<DirtyRect> dirty;
    size_t changed = 0;
    size_t uncovered = 0;
    size_t most_sparks = 0;
    size_t most_boxes = 0;
    size_t most_lit = 0;
    for (int i = 0; i < 700; i++) {
        const std::vector<uint8_t> before = frame.bytes;
        sim.step(16, frame.target, rng, dirty);
        const Diff diff = diff_frame(before, frame, dirty);
        changed += diff.changed;
        uncovered += diff.uncovered;
        most_boxes = std::max(most_boxes, dirty.size());
        most_sparks = std::max(most_sparks, sim.sparks_in_use());
        if (i % 50 == 25) {
            most_lit = std::max(most_lit, lit_pixels(frame));
        }
    }
    REQUIRE(changed > 0);
    REQUIRE(most_sparks > 0);
    CHECK(uncovered == 0);
    CHECK(most_boxes > 1);
    CHECK(most_boxes <= 2 * FireworksSim::MAX_BURSTS);
    CHECK(most_lit > 0);
}

TEST_CASE("the spark pool is sized at start, never grows, and a full pool drops sparks",
          "[screensaver][fireworks_sim]") {
    std::minstd_rand rng(5);
    FireworksSim sim;
    SimFrame frame(PixelFormat::XRGB8888);

    sim.init(frame.target, rng, 0, busy_pacing());
    CHECK(sim.spark_capacity() == static_cast<size_t>(FIREWORKS_LEVELS[0].sparks_per_burst) *
                                      FIREWORKS_LEVELS[0].bursts_at_once);

    // Sized for the cheapest level, then asked for the most expensive: one burst fills the pool.
    const size_t cheapest = FIREWORKS_LEVEL_COUNT - 1;
    sim.init(frame.target, rng, cheapest, busy_pacing());
    const size_t capacity = sim.spark_capacity();
    REQUIRE(capacity == static_cast<size_t>(FIREWORKS_LEVELS[cheapest].sparks_per_burst) *
                            FIREWORKS_LEVELS[cheapest].bursts_at_once);
    sim.request_level(0);

    std::vector<DirtyRect> dirty;
    size_t most = 0;
    size_t capacity_changes = 0;
    for (int i = 0; i < 1200; i++) {
        sim.step(16, frame.target, rng, dirty);
        capacity_changes += sim.spark_capacity() != capacity ? 1 : 0;
        most = std::max(most, sim.sparks_in_use());
    }
    CHECK(sim.level() == 0);
    CHECK(capacity_changes == 0);
    // Bursts after the pool filled found it full and dropped what did not fit.
    CHECK(most == capacity);
}

TEST_CASE("a fireworks level request waits for the next shell launch",
          "[screensaver][fireworks_sim]") {
    std::minstd_rand rng(8);
    FireworksSim sim;
    SimFrame frame(PixelFormat::RGB565);
    FireworksPacing pacing;
    pacing.first_launch_ms = 300;
    pacing.min_gap_ms = 1000;
    pacing.max_gap_ms = 1000;
    pacing.finale_every_ms = 600000;
    sim.init(frame.target, rng, 0, pacing);
    std::vector<DirtyRect> dirty;
    const auto run_ms = [&](uint32_t ms) {
        for (uint32_t t = 0; t < ms; t += 20) {
            sim.step(20, frame.target, rng, dirty);
        }
    };

    run_ms(400); // the first shell launched at 300 ms
    REQUIRE(sim.level() == 0);
    sim.request_level(2);
    run_ms(800); // 1200 ms; the next launch is due at 1300 ms
    CHECK(sim.level() == 0);
    run_ms(200); // 1400 ms
    CHECK(sim.level() == 2);

    sim.request_level(9); // clamped to the last level, again at the next launch
    run_ms(800);          // 2200 ms
    CHECK(sim.level() == 2);
    run_ms(200); // 2400 ms
    CHECK(sim.level() == FIREWORKS_LEVEL_COUNT - 1);
}

// ============================================================================
// FireworksScreensaver
// ============================================================================

TEST_CASE_METHOD(
    LVGLTestFixture,
    "the fireworks saver draws on a canvas in the build's format with the fireworks ladder",
    "[screensaver][screensaver_fireworks]") {
    FireworksScreensaver ss;
    ScreensaverStopOnExit<FireworksScreensaver> stop_on_exit{ss};
    ss.start();
    REQUIRE(ss.is_active());
    const lv_draw_buf_t* buf = lv_canvas_get_draw_buf(SaverTestAccess::canvas(ss));
    REQUIRE(buf != nullptr);
    CHECK(buf->header.cf == helix::ui::SAVER_BUILD_CANVAS_FORMAT);
    CHECK(ss.level_count() == FIREWORKS_LEVEL_COUNT);
    CHECK(FIREWORKS_LEVELS[0].period_ms == helix::ui::SAVER_FAST_PERIOD);
}

TEST_CASE_METHOD(
    LVGLTestFixture,
    "FireworksScreensaver level 0: configured period, else 16 ms, with the refresh equal",
    "[screensaver][screensaver_fireworks]") {
    const uint32_t configured_ms = GENERATE(as<uint32_t>{}, 0, 20);
    INFO("configured period " << configured_ms << " ms");
    const LevelZeroPeriods periods = level_zero_periods<FireworksScreensaver>(configured_ms);
    CHECK(periods.timer_ms == (configured_ms != 0 ? configured_ms : FIREWORKS_LEVELS[0].period_ms));
    CHECK(periods.refresh_ms == periods.timer_ms);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "a fireworks level request changes the frame period when the next shell launches",
                 "[screensaver][screensaver_fireworks]") {
    FireworksScreensaver ss;
    ScreensaverStopOnExit<FireworksScreensaver> stop_on_exit{ss};
    SaverTestAccess::set_fixed_seed(ss, 4);
    ss.start();
    REQUIRE(ss.is_active());

    ss.request_level(3);
    CHECK(ss.level() == 0);
    int frames = 0;
    while (ss.level() != 3 && frames < 400) {
        lv_tick_inc(50);
        fire(SaverTestAccess::timer(ss));
        frames++;
    }
    REQUIRE(ss.level() == 3);
    CHECK(frames > 0);
    CHECK(SaverTestAccess::timer(ss)->period == FIREWORKS_LEVELS[3].period_ms);
}

TEST_CASE_METHOD(LVGLTestFixture, "every registered saver this build draws starts from the manager",
                 "[screensaver][screensaver_fireworks]") {
    helix::ScopedEnv budget{"HELIX_SCREENSAVER_BUDGET_PCT"};
    helix::ScopedEnv level{"HELIX_SCREENSAVER_LEVEL"};
    unsetenv("HELIX_SCREENSAVER_BUDGET_PCT");
    unsetenv("HELIX_SCREENSAVER_LEVEL");
    helix::Config* config = helix::Config::get_instance();
    if (config->try_get_json("/display/screensaver_levels") != nullptr) {
        config->get_json("/display").erase("screensaver_levels");
    }

    auto& mgr = ScreensaverManager::instance();
    size_t started = 0;
    for (const helix::ui::ScreensaverInfo& info : helix::ui::SCREENSAVERS) {
        CAPTURE(info.name);
        if ((info.depths & helix::ui::BUILD_SAVER_DEPTH) == 0) {
            continue;
        }
        mgr.start(info.type);
        CHECK(mgr.is_active());
        // Every registered saver runs on SaverBase, so the gate can measure it.
        CHECK(helix::ScreensaverManagerTestAccess::active(mgr) != nullptr);
        started += 1;
        mgr.stop();
    }
    CHECK(started > 0);
    const helix::ui::ScreensaverInfo* fireworks = helix::ui::find_screensaver_by_name("fireworks");
    REQUIRE(fireworks != nullptr);
    CHECK(fireworks->type == ScreensaverType::FIREWORKS);
    CHECK((fireworks->depths & helix::ui::SAVER_DEPTH_16) != 0);
    CHECK((fireworks->depths & helix::ui::SAVER_DEPTH_32) != 0);
}

#endif // HELIX_ENABLE_SCREENSAVER
