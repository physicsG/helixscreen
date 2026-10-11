// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_ENABLE_SCREENSAVER

#include "../lvgl_test_fixture.h"
#include "../test_helpers/screensaver_test_access.h"
#include "screensaver.h"

#include "../catch_amalgamated.hpp"

// ============================================================================
// Canvas draw-buffer stride contract (prestonbrown/helixscreen#1591)
//
// lv_canvas_set_buffer() derives its own row stride (rounded to
// LV_DRAW_BUF_STRIDE_ALIGN) and sizes the canvas extent from stride * h. The
// allocation handed to it must cover that extent, or lv_canvas_fill_bg()
// overruns the buffer. These tests pin the agreement between the screensaver
// allocation and the canvas LVGL actually built.
// ============================================================================

// A width whose tight pitch differs from LVGL's stride at any alignment above
// 1 byte: 630 * 4 = 2520 rounds up to 2528 at a 16-byte align. The Android
// resize path produces such widths on fold/unfold.
namespace {
constexpr int STRAINED_W = 630;
constexpr int TEST_H = 480;

lv_obj_t* find_screensaver_canvas() {
    lv_obj_t* top = lv_layer_top();
    REQUIRE(lv_obj_get_child_count(top) > 0);
    lv_obj_t* overlay = lv_obj_get_child(top, -1); // the screensaver's overlay
    REQUIRE(lv_obj_get_child_count(overlay) >= 1);
    return lv_obj_get_child(overlay, 0); // the canvas
}
} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "starfield canvas allocation covers LVGL's stride-derived extent",
                 "[screensaver][1591]") {
    ScopedResolution res(lv_display_get_default(), STRAINED_W, TEST_H);

    StarfieldScreensaver ss;
    ss.start();
    REQUIRE(ss.is_active());

    lv_draw_buf_t* cbuf = lv_canvas_get_draw_buf(find_screensaver_canvas());
    REQUIRE(cbuf != nullptr);
    // The walk render_frame() uses and the stride lv_canvas_set_buffer()
    // recorded must be the same number.
    REQUIRE(SaverTestAccess::draw_buf_stride(ss) == cbuf->header.stride);
    // And the allocation must cover the extent LVGL will write.
    REQUIRE(SaverTestAccess::draw_buf_size(ss) >= cbuf->data_size);

    ss.stop();
    REQUIRE_FALSE(ss.is_active());
}

TEST_CASE_METHOD(LVGLTestFixture, "pipes canvas allocation covers LVGL's stride-derived extent",
                 "[screensaver][1591]") {
    ScopedResolution res(lv_display_get_default(), STRAINED_W, TEST_H);

    PipesScreensaver ss;
    ss.start();
    REQUIRE(ss.is_active());

    lv_draw_buf_t* cbuf = lv_canvas_get_draw_buf(find_screensaver_canvas());
    REQUIRE(cbuf != nullptr);
    REQUIRE(SaverTestAccess::draw_buf_size(ss) >= cbuf->data_size);

    ss.stop();
    REQUIRE_FALSE(ss.is_active());
}

TEST_CASE("screensaver_canvas_stride_bytes matches lv_canvas_set_buffer's stride",
          "[screensaver][1591]") {
    // The helper must agree with LVGL's own computation at exactly the widths
    // where a tight w * bytes-per-pixel pitch diverges from it under a
    // padded-stride config, for the format the canvas is created in.
    for (lv_color_format_t cf : {LV_COLOR_FORMAT_XRGB8888, LV_COLOR_FORMAT_RGB565}) {
        for (int w : {1, 3, 100, 480, 630, 800, 1024}) {
            CAPTURE(static_cast<int>(cf), w);
            REQUIRE(helix::ui::screensaver_canvas_stride_bytes(w, cf) ==
                    lv_draw_buf_width_to_stride(w, cf));
        }
    }
}

// ============================================================================
// Canvas pixel format
//
// The starfield canvas is opaque XRGB8888, so the full-screen canvas draws as a
// copy instead of a blend. Production 32 bpp displays run ARGB8888 and copy an
// XRGB8888 image byte for byte, so a canvas pixel whose X byte is not 0xFF draws
// transparent there (docs/devel/GPU_ACCELERATION.md, "The alpha trap"). The test
// display ignores that byte, so these tests read it from the raw canvas buffer.
// The pipes canvas is ARGB8888: see test_screensaver_pipes_canvas.cpp.
// ============================================================================

namespace {

void run_timer(lv_timer_t* timer) {
    REQUIRE(timer != nullptr);
    REQUIRE(timer->timer_cb != nullptr);
    timer->timer_cb(timer);
}

const lv_draw_buf_t* four_byte_canvas_buf(lv_obj_t* canvas) {
    REQUIRE(canvas != nullptr);
    const lv_draw_buf_t* buf = lv_canvas_get_draw_buf(canvas);
    REQUIRE(buf != nullptr);
    REQUIRE(lv_color_format_get_size(static_cast<lv_color_format_t>(buf->header.cf)) == 4);
    return buf;
}

/// Pixels of the canvas whose fourth byte is not 0xFF.
size_t pixels_not_opaque(lv_obj_t* canvas) {
    const lv_draw_buf_t* buf = four_byte_canvas_buf(canvas);
    size_t count = 0;
    for (uint32_t y = 0; y < buf->header.h; y++) {
        const uint8_t* row = buf->data + y * buf->header.stride;
        for (uint32_t x = 0; x < buf->header.w; x++) {
            if (row[x * 4 + 3] != 0xFF) {
                count++;
            }
        }
    }
    return count;
}

/// Pixels of the canvas with any colour, so a check over the buffer covers drawn pixels too.
size_t lit_pixels(lv_obj_t* canvas) {
    const lv_draw_buf_t* buf = four_byte_canvas_buf(canvas);
    size_t count = 0;
    for (uint32_t y = 0; y < buf->header.h; y++) {
        const uint8_t* row = buf->data + y * buf->header.stride;
        for (uint32_t x = 0; x < buf->header.w; x++) {
            if (row[x * 4] != 0 || row[x * 4 + 1] != 0 || row[x * 4 + 2] != 0) {
                count++;
            }
        }
    }
    return count;
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "the starfield canvas is opaque XRGB8888",
                 "[screensaver][screensaver_canvas]") {
    StarfieldScreensaver ss;
    ScreensaverStopOnExit<StarfieldScreensaver> stop_on_exit{ss};
    ss.start();
    REQUIRE(ss.is_active());
    const lv_draw_buf_t* buf = lv_canvas_get_draw_buf(SaverTestAccess::canvas(ss));
    REQUIRE(buf != nullptr);
    CHECK(buf->header.cf == LV_COLOR_FORMAT_XRGB8888);
}

TEST_CASE_METHOD(LVGLTestFixture, "every starfield canvas pixel stays opaque as stars move",
                 "[screensaver][screensaver_canvas]") {
    StarfieldScreensaver ss;
    ScreensaverStopOnExit<StarfieldScreensaver> stop_on_exit{ss};
    SaverTestAccess::set_fixed_seed(ss, 3);
    ss.start();
    REQUIRE(ss.is_active());
    lv_obj_t* canvas = SaverTestAccess::canvas(ss);
    CHECK(pixels_not_opaque(canvas) == 0);

    for (int frame = 0; frame < 30; frame++) {
        lv_tick_inc(33);
        run_timer(SaverTestAccess::timer(ss));
    }
    REQUIRE(lit_pixels(canvas) > 0);
    CHECK(pixels_not_opaque(canvas) == 0);
}

#endif // HELIX_ENABLE_SCREENSAVER
