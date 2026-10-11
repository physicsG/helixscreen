// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_ENABLE_SCREENSAVER

#include "../lvgl_test_fixture.h"
#include "../test_helpers/screensaver_test_access.h"
#include "screensaver_pipes.h"

#include "../catch_amalgamated.hpp"

// ============================================================================
// Pipes canvas pixel format
//
// Pipes draws antialiased segments and joints with lv_draw_*. On aarch64, LVGL's
// NEON blends into an XRGB8888 destination write 0 into each pixel's 4th byte,
// and the canvas is copied byte for byte into the ARGB8888 display, so those
// pixels would draw transparent. The pipes canvas is ARGB8888 and keeps real
// alpha. The x86 blends never write that byte, so the alpha check cannot see the
// NEON path on a host build; the format assertion is what does.
// ============================================================================

namespace {

void run_timer(lv_timer_t* timer) {
    REQUIRE(timer != nullptr);
    REQUIRE(timer->timer_cb != nullptr);
    timer->timer_cb(timer);
}

const lv_draw_buf_t* pipes_canvas_buf(const PipesScreensaver& ss) {
    lv_obj_t* canvas = SaverTestAccess::canvas(ss);
    REQUIRE(canvas != nullptr);
    const lv_draw_buf_t* buf = lv_canvas_get_draw_buf(canvas);
    REQUIRE(buf != nullptr);
    REQUIRE(lv_color_format_get_size(static_cast<lv_color_format_t>(buf->header.cf)) == 4);
    return buf;
}

struct PixelCounts {
    size_t lit = 0;        ///< pixels with any colour
    size_t not_opaque = 0; ///< pixels whose 4th byte is not 0xFF
};

PixelCounts count_pixels(const lv_draw_buf_t* buf) {
    PixelCounts counts;
    for (uint32_t y = 0; y < buf->header.h; y++) {
        const uint8_t* row = buf->data + y * buf->header.stride;
        for (uint32_t x = 0; x < buf->header.w; x++) {
            const uint8_t* px = row + x * 4;
            counts.lit += (px[0] | px[1] | px[2]) != 0 ? 1 : 0;
            counts.not_opaque += px[3] != 0xFF ? 1 : 0;
        }
    }
    return counts;
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "the pipes canvas is ARGB8888",
                 "[screensaver][screensaver_canvas][pipes_canvas]") {
    PipesScreensaver ss;
    ScreensaverStopOnExit<PipesScreensaver> stop_on_exit{ss};
    ss.start();
    REQUIRE(ss.is_active());
    CHECK(pipes_canvas_buf(ss)->header.cf == LV_COLOR_FORMAT_ARGB8888);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "every pipes canvas pixel keeps alpha 0xFF as pipes grow and the grid resets",
                 "[screensaver][screensaver_canvas][pipes_canvas]") {
    PipesScreensaver ss;
    ScreensaverStopOnExit<PipesScreensaver> stop_on_exit{ss};
    SaverTestAccess::set_fixed_seed(ss, 21);
    ss.start();
    REQUIRE(ss.is_active());
    const lv_draw_buf_t* buf = pipes_canvas_buf(ss);
    PixelCounts counts = count_pixels(buf);
    REQUIRE(counts.lit > 0);
    CHECK(counts.not_opaque == 0);

    // One grow step per 100 ms, drawing antialiased segments and joints.
    for (int step = 0; step < 30; step++) {
        lv_tick_inc(100);
        run_timer(SaverTestAccess::timer(ss));
    }
    counts = count_pixels(buf);
    REQUIRE(counts.lit > 0);
    CHECK(counts.not_opaque == 0);

    PipesScreensaverTestAccess::set_total_segments(ss,
                                                   PipesScreensaverTestAccess::max_segments() + 1);
    lv_tick_inc(100);
    run_timer(SaverTestAccess::timer(ss));
    REQUIRE(PipesScreensaverTestAccess::total_segments(ss) == 0);
    counts = count_pixels(buf);
    REQUIRE(counts.lit > 0);
    CHECK(counts.not_opaque == 0);
}

#endif // HELIX_ENABLE_SCREENSAVER
