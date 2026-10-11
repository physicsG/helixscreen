// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_ENABLE_SCREENSAVER

#include "../lvgl_test_fixture.h"
#include "../test_helpers/refresh_period_hold_test_access.h"
#include "../test_helpers/screensaver_test_access.h"
#include "refresh_period_hold.h"
#include "screensaver_base.h"
#include "screensaver_canvas.h"
#include "screensaver_frame.h"
#include "screensaver_frame_timer.h"
#include "screensaver_overlay.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::ui::DirtyRect;
using helix::ui::SAVER_MAX_DIRTY_AREAS;
using helix::ui::SaverCanvas;
using helix::ui::SaverFrameTimer;
using helix::ui::SaverOverlay;

namespace {

bool box_covers(const std::vector<DirtyRect>& boxes, const DirtyRect& r) {
    return std::any_of(boxes.begin(), boxes.end(), [&](const DirtyRect& b) {
        return b.x1 <= r.x1 && b.y1 <= r.y1 && b.x2 >= r.x2 && b.y2 >= r.y2;
    });
}

bool areas_cover_point(const std::vector<lv_area_t>& areas, int32_t x, int32_t y) {
    return std::any_of(areas.begin(), areas.end(), [&](const lv_area_t& a) {
        return x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2;
    });
}

void fire(lv_timer_t* timer) {
    REQUIRE(timer != nullptr);
    REQUIRE(timer->timer_cb != nullptr);
    timer->timer_cb(timer);
}

lv_area_t coords_of(lv_obj_t* obj) {
    lv_obj_update_layout(obj);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    return area;
}

constexpr uint32_t PROBE_PERIODS_MS[] = {helix::ui::SAVER_FAST_PERIOD, 45, 50};

/// A saver that records what the base asks of it.
class ProbeSaver : public helix::ui::SaverBase {
  public:
    ScreensaverType type() const override {
        return ScreensaverType::STARFIELD;
    }
    using SaverBase::apply_level;

    std::optional<lv_color_format_t> format = LV_COLOR_FORMAT_XRGB8888;
    bool refuse = false;
    bool defer_levels = false;
    bool canvas_at_start = false;
    size_t level_at_start = 99;
    uint32_t first_random = 0;
    std::vector<std::string> calls;
    std::vector<uint32_t> frame_dts;
    std::vector<DirtyRect> frame_dirty;
    std::vector<size_t> level_requests;

  protected:
    std::optional<lv_color_format_t> canvas_format() const override {
        return format;
    }
    bool on_start() override {
        calls.emplace_back("start");
        canvas_at_start = canvas().obj() != nullptr;
        level_at_start = level();
        first_random = rng()();
        return !refuse;
    }
    void on_frame(uint32_t dt_ms, std::vector<DirtyRect>& dirty) override {
        calls.emplace_back("frame");
        frame_dts.push_back(dt_ms);
        dirty = frame_dirty;
    }
    void on_stop() override {
        calls.emplace_back("stop");
    }
    size_t ladder_size() const override {
        return std::size(PROBE_PERIODS_MS);
    }
    uint32_t ladder_period_ms(size_t level) const override {
        return PROBE_PERIODS_MS[level];
    }
    void on_level_request(size_t level) override {
        level_requests.push_back(level);
        if (!defer_levels) {
            apply_level(level);
        }
    }
};

} // namespace

// ============================================================================
// merge_dirty_areas
// ============================================================================

TEST_CASE("merged dirty areas number at most the limit and cover every input",
          "[screensaver][screensaver_parts]") {
    std::minstd_rand rng(17);
    std::vector<DirtyRect> input;
    for (int i = 0; i < 180; i++) {
        const auto x = static_cast<int32_t>(rng() % 780);
        const auto y = static_cast<int32_t>(rng() % 460);
        input.push_back(
            {x, y, x + static_cast<int32_t>(rng() % 20), y + static_cast<int32_t>(rng() % 20)});
    }
    REQUIRE(input.size() > SAVER_MAX_DIRTY_AREAS);

    std::vector<DirtyRect> merged = input;
    helix::ui::merge_dirty_areas(merged, SAVER_MAX_DIRTY_AREAS);

    CHECK(merged.size() == SAVER_MAX_DIRTY_AREAS);
    for (const DirtyRect& r : input) {
        CHECK(box_covers(merged, r));
    }
}

TEST_CASE("merging within the limit keeps the areas in order and drops empty ones",
          "[screensaver][screensaver_parts]") {
    std::vector<DirtyRect> areas = {{0, 0, 9, 9}, {}, {100, 100, 120, 110}, {50, 5, 60, 6}};
    helix::ui::merge_dirty_areas(areas, SAVER_MAX_DIRTY_AREAS);
    REQUIRE(areas.size() == 3);
    CHECK(areas[0] == DirtyRect{0, 0, 9, 9});
    CHECK(areas[1] == DirtyRect{100, 100, 120, 110});
    CHECK(areas[2] == DirtyRect{50, 5, 60, 6});
}

TEST_CASE("merging joins the pair that adds the fewest pixels",
          "[screensaver][screensaver_parts]") {
    std::vector<DirtyRect> areas = {{0, 0, 9, 9}, {10, 0, 19, 9}, {500, 400, 509, 409}};
    helix::ui::merge_dirty_areas(areas, 2);
    REQUIRE(areas.size() == 2);
    CHECK(areas[0] == DirtyRect{0, 0, 19, 9});
    CHECK(areas[1] == DirtyRect{500, 400, 509, 409});
}

// ============================================================================
// SaverOverlay
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture,
                 "a saver overlay is an opaque black touch-absorbing child of the top layer",
                 "[screensaver][screensaver_parts]") {
    SaverOverlay overlay;
    overlay.create();
    lv_obj_t* obj = overlay.obj();
    REQUIRE(obj != nullptr);
    CHECK(lv_obj_get_parent(obj) == lv_layer_top());
    CHECK(lv_obj_get_style_bg_opa(obj, LV_PART_MAIN) == LV_OPA_COVER);
    CHECK(lv_color_eq(lv_obj_get_style_bg_color(obj, LV_PART_MAIN), lv_color_black()));
    CHECK(lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE));
    CHECK_FALSE(lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE));

    overlay.make_transparent();
    CHECK(lv_obj_get_style_bg_opa(obj, LV_PART_MAIN) == LV_OPA_TRANSP);

    overlay.destroy();
    CHECK(overlay.obj() == nullptr);
    // Deleted on a later timer pass, hidden until then.
    CHECK(lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN));
}

// ============================================================================
// SaverCanvas
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture,
                 "a saver canvas allocates at LVGL's stride for its format and starts opaque black",
                 "[screensaver][screensaver_parts]") {
    const lv_color_format_t cf = GENERATE(as<lv_color_format_t>{}, LV_COLOR_FORMAT_XRGB8888,
                                          LV_COLOR_FORMAT_ARGB8888, LV_COLOR_FORMAT_RGB565);
    const int32_t w = GENERATE(630, 800);
    CAPTURE(static_cast<int>(cf), w);
    constexpr int32_t H = 120;

    SaverOverlay overlay;
    overlay.create();
    SaverCanvas canvas;
    REQUIRE(canvas.create(overlay.obj(), w, H, cf));
    const lv_draw_buf_t* buf = lv_canvas_get_draw_buf(canvas.obj());
    REQUIRE(buf != nullptr);
    CHECK(buf->header.cf == cf);
    CHECK(canvas.format() == cf);
    CHECK(canvas.width() == w);
    CHECK(canvas.height() == H);
    CHECK(canvas.stride() == lv_draw_buf_width_to_stride(static_cast<uint32_t>(w), cf));
    CHECK(canvas.stride() == buf->header.stride);
    CHECK(canvas.buffer_size() == static_cast<size_t>(canvas.stride()) * H);
    CHECK(canvas.buffer_size() >= buf->data_size);

    const uint32_t bytes = lv_color_format_get_size(cf);
    size_t not_black = 0;
    for (int32_t y = 0; y < H; y++) {
        const uint8_t* row = canvas.data() + static_cast<size_t>(y) * canvas.stride();
        for (int32_t x = 0; x < w; x++) {
            const uint8_t* px = row + static_cast<size_t>(x) * bytes;
            if (bytes == 2) {
                not_black += (px[0] | px[1]) != 0 ? 1 : 0;
            } else {
                not_black += ((px[0] | px[1] | px[2]) != 0 || px[3] != 0xFF) ? 1 : 0;
            }
        }
    }
    CHECK(not_black == 0);

    canvas.release();
    overlay.destroy();
}

TEST_CASE_METHOD(LVGLTestFixture, "releasing a saver canvas hides it and frees its buffer",
                 "[screensaver][screensaver_parts]") {
    SaverOverlay overlay;
    overlay.create();
    SaverCanvas canvas;
    REQUIRE(canvas.create(overlay.obj(), 64, 48, LV_COLOR_FORMAT_XRGB8888));
    lv_obj_t* obj = canvas.obj();

    canvas.release();

    CHECK(lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN));
    CHECK(canvas.obj() == nullptr);
    CHECK(canvas.data() == nullptr);
    CHECK(canvas.buffer_size() == 0);
    overlay.destroy();
}

TEST_CASE_METHOD(
    LVGLTestFixture,
    "a saver canvas invalidates at most SAVER_MAX_DIRTY_AREAS areas covering every dirty box",
    "[screensaver][screensaver_parts]") {
    SaverOverlay overlay;
    overlay.create();
    SaverCanvas canvas;
    REQUIRE(canvas.create(overlay.obj(), 800, 480, LV_COLOR_FORMAT_XRGB8888));
    const lv_area_t coords = coords_of(canvas.obj());

    std::vector<DirtyRect> dirty;
    for (int32_t i = 0; i < 200; i++) {
        // A stride-coprime scatter over the canvas, so every box stays on it.
        const int32_t x = (i * 37) % 790;
        const int32_t y = (i * 53) % 470;
        dirty.push_back({x, y, x + 2, y + 2});
    }
    const std::vector<DirtyRect> input = dirty;
    helix::test::InvalidatedAreas invalidated(lv_obj_get_display(canvas.obj()));

    canvas.invalidate(dirty);

    REQUIRE_FALSE(invalidated.areas.empty());
    CHECK(invalidated.areas.size() <= SAVER_MAX_DIRTY_AREAS);
    for (const DirtyRect& r : input) {
        CHECK(areas_cover_point(invalidated.areas, coords.x1 + r.x1, coords.y1 + r.y1));
        CHECK(areas_cover_point(invalidated.areas, coords.x1 + r.x2, coords.y1 + r.y2));
    }
    canvas.release();
    overlay.destroy();
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "a saver canvas layer session invalidates only the areas it marked",
                 "[screensaver][screensaver_parts]") {
    SaverOverlay overlay;
    overlay.create();
    SaverCanvas canvas;
    REQUIRE(canvas.create(overlay.obj(), 200, 100, LV_COLOR_FORMAT_ARGB8888));
    const lv_area_t coords = coords_of(canvas.obj());
    helix::test::InvalidatedAreas invalidated(lv_obj_get_display(canvas.obj()));

    lv_layer_t layer;
    canvas.begin_layer(&layer);
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_white();
    dsc.bg_opa = LV_OPA_COVER;
    const lv_area_t rect = {10, 20, 29, 39};
    lv_draw_rect(&layer, &dsc, &rect);
    canvas.mark_dirty(10, 20, 29, 39);
    canvas.finish_layer(&layer);

    REQUIRE(invalidated.areas.size() == 1);
    CHECK(invalidated.areas[0].x1 == coords.x1 + 10);
    CHECK(invalidated.areas[0].y1 == coords.y1 + 20);
    CHECK(invalidated.areas[0].x2 == coords.x1 + 29);
    CHECK(invalidated.areas[0].y2 == coords.y1 + 39);
    // The session drew the rect into the buffer.
    const uint8_t* px = canvas.data() + 25 * static_cast<size_t>(canvas.stride()) + 15 * 4;
    CHECK(px[0] == 0xFF);
    CHECK(px[1] == 0xFF);
    CHECK(px[2] == 0xFF);
    canvas.release();
    overlay.destroy();
}

// ============================================================================
// SaverFrameTimer
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture,
                 "a saver frame timer calls back with the time since its previous call",
                 "[screensaver][screensaver_parts]") {
    SaverFrameTimer timer;
    std::vector<uint32_t> dts;
    timer.start(20, [&](uint32_t dt_ms) { dts.push_back(dt_ms); });
    REQUIRE(timer.timer() != nullptr);
    CHECK(timer.timer()->period == 20);

    lv_tick_inc(25);
    fire(timer.timer());
    lv_tick_inc(7);
    fire(timer.timer());

    CHECK(dts == std::vector<uint32_t>{25, 7});
    timer.cancel();
}

TEST_CASE_METHOD(LVGLTestFixture, "changing a saver frame timer's period keeps motion continuous",
                 "[screensaver][screensaver_parts]") {
    SaverFrameTimer timer;
    uint32_t last_dt = 0;
    timer.start(16, [&](uint32_t dt_ms) { last_dt = dt_ms; });
    lv_tick_inc(10);
    timer.set_period(33);
    CHECK(timer.timer()->period == 33);
    lv_tick_inc(15);
    fire(timer.timer());
    CHECK(last_dt == 25);
    timer.cancel();
}

TEST_CASE_METHOD(LVGLTestFixture, "cancelling a saver frame timer neuters it",
                 "[screensaver][screensaver_parts]") {
    SaverFrameTimer timer;
    int calls = 0;
    timer.start(16, [&](uint32_t) { calls++; });
    lv_timer_t* raw = timer.timer();
    REQUIRE(raw != nullptr);

    timer.cancel();

    CHECK(timer.timer() == nullptr);
    // lv_timer_cancel_safe() clears the callback; lv_timer_handler deletes the timer later.
    CHECK(raw->timer_cb == nullptr);
    CHECK(calls == 0);
}

// ============================================================================
// SaverBase
// ============================================================================

TEST_CASE_METHOD(
    LVGLTestFixture,
    "a saver starts with its overlay, canvas, seeded random sequence and a 16 ms timer",
    "[screensaver][screensaver_parts]") {
    // With no period configured, level 0 runs at SAVER_FAST_PERIOD whatever the display refresh.
    ScopedRefreshPeriod refresh(20);
    ScopedGlobalRefreshHold clean_hold;
    ProbeSaver saver;
    ScreensaverStopOnExit<ProbeSaver> stop_on_exit{saver};
    SaverTestAccess::set_fixed_seed(saver, 7);

    saver.start();

    REQUIRE(saver.is_active());
    CHECK(saver.canvas_at_start);
    lv_obj_t* overlay = SaverTestAccess::overlay(saver);
    REQUIRE(overlay != nullptr);
    CHECK(lv_obj_get_parent(overlay) == lv_layer_top());
    CHECK(lv_obj_get_style_bg_opa(overlay, LV_PART_MAIN) == LV_OPA_TRANSP);
    lv_obj_t* canvas = SaverTestAccess::canvas(saver);
    REQUIRE(canvas != nullptr);
    CHECK(lv_obj_get_parent(canvas) == overlay);
    REQUIRE(SaverTestAccess::timer(saver) != nullptr);
    CHECK(SaverTestAccess::timer(saver)->period == helix::ui::SAVER_FAST_PERIOD);
    std::minstd_rand expected(7);
    CHECK(saver.first_random == expected());
    CHECK(saver.calls == std::vector<std::string>{"start"});
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "a saver without a canvas keeps its overlay opaque and invalidates nothing",
                 "[screensaver][screensaver_parts]") {
    ProbeSaver saver;
    ScreensaverStopOnExit<ProbeSaver> stop_on_exit{saver};
    saver.format = std::nullopt;
    saver.frame_dirty = {{1, 1, 2, 2}};

    saver.start();

    REQUIRE(saver.is_active());
    CHECK_FALSE(saver.canvas_at_start);
    CHECK(SaverTestAccess::canvas(saver) == nullptr);
    CHECK(lv_obj_get_style_bg_opa(SaverTestAccess::overlay(saver), LV_PART_MAIN) == LV_OPA_COVER);
    helix::test::InvalidatedAreas invalidated(lv_display_get_default());
    lv_tick_inc(16);
    fire(SaverTestAccess::timer(saver));
    CHECK(saver.frame_dts == std::vector<uint32_t>{16});
    CHECK(invalidated.areas.empty());
}

TEST_CASE_METHOD(LVGLTestFixture, "a saver that refuses to start leaves nothing running",
                 "[screensaver][screensaver_parts]") {
    ProbeSaver saver;
    saver.refuse = true;

    saver.start();

    CHECK_FALSE(saver.is_active());
    CHECK(SaverTestAccess::timer(saver) == nullptr);
    CHECK(SaverTestAccess::overlay(saver) == nullptr);
    CHECK(SaverTestAccess::canvas(saver) == nullptr);
    CHECK(SaverTestAccess::draw_buf_size(saver) == 0);
    CHECK(saver.calls == std::vector<std::string>{"start"});
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "a saver frame gets the elapsed time and its dirty areas are invalidated",
                 "[screensaver][screensaver_parts]") {
    ProbeSaver saver;
    ScreensaverStopOnExit<ProbeSaver> stop_on_exit{saver};
    saver.frame_dirty = {{5, 6, 7, 8}};
    saver.start();
    REQUIRE(saver.is_active());
    const lv_area_t coords = coords_of(SaverTestAccess::canvas(saver));
    helix::test::InvalidatedAreas invalidated(lv_display_get_default());

    lv_tick_inc(30);
    fire(SaverTestAccess::timer(saver));

    CHECK(saver.frame_dts == std::vector<uint32_t>{30});
    REQUIRE(invalidated.areas.size() == 1);
    CHECK(invalidated.areas[0].x1 == coords.x1 + 5);
    CHECK(invalidated.areas[0].y1 == coords.y1 + 6);
    CHECK(invalidated.areas[0].x2 == coords.x1 + 7);
    CHECK(invalidated.areas[0].y2 == coords.y1 + 8);
}

TEST_CASE_METHOD(LVGLTestFixture, "a saver starts at its start level, clamped to its ladder",
                 "[screensaver][screensaver_parts]") {
    ProbeSaver saver;
    ScreensaverStopOnExit<ProbeSaver> stop_on_exit{saver};

    saver.set_start_level(2);
    saver.start();
    REQUIRE(saver.is_active());
    CHECK(saver.level() == 2);
    CHECK(saver.level_at_start == 2);
    CHECK(SaverTestAccess::timer(saver)->period == 50);
    CHECK(saver.level_count() == 3);
    saver.stop();

    saver.set_start_level(9);
    saver.start();
    REQUIRE(saver.is_active());
    CHECK(saver.level() == 2);
}

TEST_CASE_METHOD(
    LVGLTestFixture,
    "a level request goes through the saver, which moves its timer and the held refresh",
    "[screensaver][screensaver_parts]") {
    helix::ScopedTimerPeriods restore;
    helix::ScopedTimerPeriods::set(40, 40);
    ScopedGlobalRefreshHold clean_hold;
    helix::RefreshPeriodHold& hold = helix::active_refresh_period_hold();
    ProbeSaver saver;
    ScreensaverStopOnExit<ProbeSaver> stop_on_exit{saver};

    saver.request_level(1);
    CHECK(saver.level_requests.empty()); // not running

    hold.acquire();
    saver.start();
    REQUIRE(saver.is_active());
    CHECK(SaverTestAccess::timer(saver)->period == helix::ui::SAVER_FAST_PERIOD);
    CHECK(helix::default_refr_timer_period() == helix::ui::SAVER_FAST_PERIOD);

    saver.request_level(1);
    CHECK(saver.level_requests == std::vector<size_t>{1});
    CHECK(saver.level() == 1);
    CHECK(SaverTestAccess::timer(saver)->period == 45);
    CHECK(helix::default_refr_timer_period() == 45);

    saver.defer_levels = true;
    saver.request_level(2);
    CHECK(saver.level_requests == std::vector<size_t>{1, 2});
    CHECK(saver.level() == 1);
    CHECK(SaverTestAccess::timer(saver)->period == 45);
    saver.apply_level(2);
    CHECK(saver.level() == 2);
    CHECK(SaverTestAccess::timer(saver)->period == 50);
    CHECK(helix::default_refr_timer_period() == 50);
    CHECK(saver.level_period_ms(9) == 50);

    saver.request_level(9);
    CHECK(saver.level_requests.back() == 2);

    saver.stop();
    hold.release();
    CHECK(helix::default_refr_timer_period() == 40);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "a configured refresh period replaces level 0's period and no other level's",
                 "[screensaver][screensaver_parts]") {
    helix::ScopedTimerPeriods restore;
    helix::ScopedTimerPeriods::set(40, 40);
    ScopedGlobalRefreshHold clean_hold;
    helix::RefreshPeriodHold& hold = helix::active_refresh_period_hold();
    hold.set_period(20);
    ProbeSaver saver;
    ScreensaverStopOnExit<ProbeSaver> stop_on_exit{saver};
    CHECK(saver.level_period_ms(0) == 20);
    CHECK(saver.level_period_ms(1) == 45);
    CHECK(saver.level_period_ms(2) == 50);

    hold.acquire();
    saver.start();
    REQUIRE(saver.is_active());
    CHECK(SaverTestAccess::timer(saver)->period == 20);
    CHECK(helix::default_refr_timer_period() == 20);

    saver.request_level(1);
    CHECK(SaverTestAccess::timer(saver)->period == 45);
    CHECK(helix::default_refr_timer_period() == 45);

    saver.stop();
    hold.release();
    CHECK(helix::default_refr_timer_period() == 40);
}

TEST_CASE_METHOD(
    LVGLTestFixture,
    "stopping a saver cancels its timer, hides and frees its canvas, and queues its overlay",
    "[screensaver][screensaver_parts]") {
    ProbeSaver saver;
    saver.start();
    REQUIRE(saver.is_active());
    lv_obj_t* canvas = SaverTestAccess::canvas(saver);
    lv_obj_t* overlay = SaverTestAccess::overlay(saver);
    lv_timer_t* timer = SaverTestAccess::timer(saver);

    saver.stop();

    CHECK_FALSE(saver.is_active());
    CHECK(timer->timer_cb == nullptr);
    CHECK(SaverTestAccess::timer(saver) == nullptr);
    CHECK(lv_obj_has_flag(canvas, LV_OBJ_FLAG_HIDDEN));
    CHECK(SaverTestAccess::draw_buf_size(saver) == 0);
    CHECK(SaverTestAccess::overlay(saver) == nullptr);
    CHECK(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN));
    CHECK(saver.calls == std::vector<std::string>{"start", "stop"});
}

TEST_CASE_METHOD(LVGLTestFixture, "a restarted saver replays its fixed seed",
                 "[screensaver][screensaver_parts]") {
    ProbeSaver saver;
    ScreensaverStopOnExit<ProbeSaver> stop_on_exit{saver};
    SaverTestAccess::set_fixed_seed(saver, 1234);
    saver.start();
    const uint32_t first = saver.first_random;
    saver.stop();
    saver.start();
    CHECK(saver.first_random == first);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "a saver canvas frame describes its buffer in the matching pixel format",
                 "[screensaver][screensaver_parts]") {
    for (const auto& [cf, format] :
         {std::pair<lv_color_format_t, helix::ui::PixelFormat>{LV_COLOR_FORMAT_XRGB8888,
                                                               helix::ui::PixelFormat::XRGB8888},
          std::pair<lv_color_format_t, helix::ui::PixelFormat>{LV_COLOR_FORMAT_RGB565,
                                                               helix::ui::PixelFormat::RGB565}}) {
        INFO("canvas format " << static_cast<int>(cf));
        SaverOverlay overlay;
        overlay.create();
        SaverCanvas canvas;
        REQUIRE(canvas.create(overlay.obj(), 96, 40, cf));
        const helix::ui::FrameTarget frame = canvas.frame();
        CHECK(frame.format == format);
        CHECK(frame.data == canvas.data());
        CHECK(frame.stride == canvas.stride());
        CHECK(frame.w == 96u);
        CHECK(frame.h == 40u);
        canvas.release();
        overlay.destroy();
    }
    CHECK(helix::ui::pixel_format_for(LV_COLOR_FORMAT_RGB565) == helix::ui::PixelFormat::RGB565);
    CHECK(helix::ui::pixel_format_for(LV_COLOR_FORMAT_XRGB8888) ==
          helix::ui::PixelFormat::XRGB8888);
}
// A saver whose dirty areas cover nearly the whole canvas is cheaper to invalidate whole: a
// double-buffered display syncs whatever the partial invalidation left out, strip by strip,
// on every frame. Coverage counts covered pixels: each area is rendered and flushed on its
// own, so scattered small areas cost their sum however far apart they lie.
TEST_CASE("a near-full dirty box invalidates the whole canvas",
          "[screensaver][screensaver_parts]") {
    using helix::ui::covers_whole_canvas;
    using helix::ui::DirtyRect;

    SECTION("the whole canvas counts") {
        CHECK(covers_whole_canvas(DirtyRect{0, 0, 799, 479}, 800, 480));
    }
    SECTION("a near-whole box counts") {
        // 790 x 470 of 800 x 480 is 96.7%.
        CHECK(covers_whole_canvas(DirtyRect{5, 5, 794, 474}, 800, 480));
    }
    SECTION("a half-screen box does not") {
        CHECK_FALSE(covers_whole_canvas(DirtyRect{0, 0, 799, 239}, 800, 480));
    }
    SECTION("an empty box does not") {
        CHECK_FALSE(covers_whole_canvas(DirtyRect{}, 800, 480));
    }
    SECTION("a canvas with no area never promotes") {
        CHECK_FALSE(covers_whole_canvas(DirtyRect{0, 0, 10, 10}, 0, 0));
    }
}

// Two small boxes at opposite corners span the whole canvas between them while covering a
// handful of pixels, so deciding the whole-canvas promotion on their bounding box would
// turn almost any scattered frame into a full redraw.
// The coverage rule answers at compile time, so a caller can size a buffer or assert a
// budget from it without running anything.
static_assert(helix::ui::covers_whole_canvas(helix::ui::DirtyRect{0, 0, 799, 479}, 800, 480),
              "a full-canvas box covers the canvas");
static_assert(!helix::ui::covers_whole_canvas(helix::ui::DirtyRect{0, 0, 4, 4}, 800, 480),
              "a 5x5 box does not cover the canvas");

TEST_CASE("scattered dirty boxes do not promote a whole-canvas invalidate",
          "[screensaver][screensaver_parts]") {
    using helix::ui::covers_whole_canvas;
    using helix::ui::DirtyRect;

    const DirtyRect corners[] = {{0, 0, 4, 4}, {795, 475, 799, 479}};
    int64_t covered = 0;
    DirtyRect span;
    for (const DirtyRect& r : corners) {
        covered += r.area();
        span.add(r);
    }
    // The pair spans essentially the whole canvas but covers 50 of its 384000 pixels.
    REQUIRE(span.area() * 100 >= 800 * 480 * 90);
    REQUIRE(covered * 100 < 800 * 480 * 90);
    CHECK_FALSE(covers_whole_canvas(covered, 800, 480));
    CHECK(covers_whole_canvas(covered * 8000, 800, 480));
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "a saver canvas keeps scattered dirty boxes as separate small areas",
                 "[screensaver][screensaver_parts]") {
    SaverOverlay overlay;
    overlay.create();
    SaverCanvas canvas;
    REQUIRE(canvas.create(overlay.obj(), 800, 480, LV_COLOR_FORMAT_XRGB8888));
    // Invalidations are clipped to the object's coords, which a layout pass computes.
    lv_obj_update_layout(canvas.obj());
    helix::test::InvalidatedAreas invalidated(lv_obj_get_display(canvas.obj()));

    // The two boxes span the whole canvas between them.
    std::vector<DirtyRect> dirty = {{0, 0, 4, 4}, {795, 475, 799, 479}};
    canvas.invalidate(dirty);

    REQUIRE(invalidated.areas.size() == 2);
    for (const lv_area_t& a : invalidated.areas) {
        CHECK(lv_area_get_size(&a) == 25);
    }
    canvas.release();
    overlay.destroy();
}

#endif // HELIX_ENABLE_SCREENSAVER
