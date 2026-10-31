// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// The spool illustration paints its filament the way every swatch does: the
// multi-colour split across the wound body and the finish's texture on it.
// Pixels are read straight from the canvas draw buffer.

#include "ui_spool_canvas.h"

#include "../lvgl_ui_test_fixture.h"

#include "../catch_amalgamated.hpp"

namespace {

constexpr int32_t SPOOL = 64;

lv_color32_t px(lv_obj_t* canvas, int32_t x, int32_t y) {
    const lv_draw_buf_t* buf = lv_canvas_get_draw_buf(canvas);
    REQUIRE(buf != nullptr);
    return reinterpret_cast<const lv_color32_t*>(buf->data + y * buf->header.stride)[x];
}

int brightness(lv_color32_t c) {
    return c.red + c.green + c.blue;
}

} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "spool_canvas: a multi-colour filament splits across the body",
                 "[spool_canvas][swatch]") {
    lv_obj_t* canvas = ui_spool_canvas_create(test_screen(), SPOOL);
    REQUIRE(canvas != nullptr);

    // A plain render first, so the split render must not come back from the cache.
    helix::ui::spool_canvas_set_filament(canvas, 0xFF0000, "", helix::ui::SwatchFinish::Plain);
    const lv_color32_t upper = px(canvas, 24, 15);
    const lv_color32_t lower = px(canvas, 24, 50);
    CHECK(upper.red > upper.blue);
    CHECK(lower.red > lower.blue);

    helix::ui::spool_canvas_set_filament(canvas, 0xFF0000, "#FF0000,#0000FF",
                                         helix::ui::SwatchFinish::Plain);
    // A dual-colour strand winds as thin stripes that alternate along the axis.
    // 0 = neither, 1 = red, 2 = blue.
    auto strand_of = [&](int32_t x, int32_t y) {
        const lv_color32_t c = px(canvas, x, y);
        return c.red > c.blue + 40 ? 1 : (c.blue > c.red + 40 ? 2 : 0);
    };
    int switches = 0;
    int last = 0;
    for (int32_t x = 6; x < SPOOL - 6; ++x) {
        const int s = strand_of(x, 32);
        if (s != 0 && last != 0 && s != last)
            ++switches;
        if (s != 0)
            last = s;
    }
    CHECK(switches >= 6);

    // The stripes are wrap circles in perspective, not straight lines: rows
    // away from mid-height carry the pattern at a different x.
    int moved = 0;
    for (int32_t x = 6; x < SPOOL - 6; ++x) {
        const int mid = strand_of(x, 32);
        const int high = strand_of(x, 16);
        if (mid != 0 && high != 0 && mid != high)
            ++moved;
    }
    CHECK(moved >= 3);

    // A silk strand's colours shade into each other where they meet.
    helix::ui::spool_canvas_set_filament(canvas, 0xFF0000, "#FF0000,#0000FF",
                                         helix::ui::SwatchFinish::Silk);
    bool blended = false;
    for (int32_t y = 18; y < 46 && !blended; y += 4) {
        for (int32_t x = 6; x < SPOOL - 6 && !blended; ++x) {
            const lv_color32_t c = px(canvas, x, y);
            blended = c.red > 60 && c.blue > 60 && c.green < c.red && c.green < c.blue;
        }
    }
    CHECK(blended);

    // A plain colour clears the split again.
    ui_spool_canvas_set_color(canvas, lv_color_hex(0xFF0000));
    const lv_color32_t again = px(canvas, 24, 50);
    CHECK(again.red > again.blue);
}

TEST_CASE_METHOD(LVGLUITestFixture, "spool_canvas: silk lights the body, matte mutes it",
                 "[spool_canvas][swatch]") {
    lv_obj_t* canvas = ui_spool_canvas_create(test_screen(), SPOOL);
    REQUIRE(canvas != nullptr);

    helix::ui::spool_canvas_set_filament(canvas, 0x2060C0, "", helix::ui::SwatchFinish::Plain);
    const lv_color32_t plain = px(canvas, 26, 33);
    const lv_color32_t plain_top = px(canvas, 26, 17);

    helix::ui::spool_canvas_set_filament(canvas, 0x2060C0, "", helix::ui::SwatchFinish::Silk);
    CHECK(brightness(px(canvas, 26, 33)) > brightness(plain) + 30);
    // The sheen spans the whole body, not a band across its middle.
    CHECK(brightness(px(canvas, 26, 17)) > brightness(plain_top) + 12);

    helix::ui::spool_canvas_set_filament(canvas, 0x2060C0, "", helix::ui::SwatchFinish::Matte);
    const lv_color32_t matte = px(canvas, 26, 33);
    CHECK(matte.blue - matte.red < plain.blue - plain.red);
}

namespace {

// Share of a patch of the wound body that is a dark fleck: a pixel well below
// both horizontal neighbours.
float fleck_share(lv_obj_t* canvas, int32_t size) {
    int flecks = 0;
    int total = 0;
    for (int32_t y = size * 3 / 10; y < size * 7 / 10; ++y) {
        for (int32_t x = size / 5; x < size * 9 / 20; ++x) {
            const int b = brightness(px(canvas, x, y));
            if (b < brightness(px(canvas, x - 2, y)) - 40 &&
                b < brightness(px(canvas, x + 2, y)) - 40)
                ++flecks;
            ++total;
        }
    }
    return total > 0 ? static_cast<float>(flecks) / static_cast<float>(total) : 0.0f;
}

} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "spool_canvas: marble keeps one fleck density at every size",
                 "[spool_canvas][swatch]") {
    lv_obj_t* small = ui_spool_canvas_create(test_screen(), 120);
    lv_obj_t* large = ui_spool_canvas_create(test_screen(), 240);
    REQUIRE(small != nullptr);
    REQUIRE(large != nullptr);
    helix::ui::spool_canvas_set_filament(small, 0xE8E4DC, "", helix::ui::SwatchFinish::Marble);
    helix::ui::spool_canvas_set_filament(large, 0xE8E4DC, "", helix::ui::SwatchFinish::Marble);

    const float a = fleck_share(small, 120);
    const float b = fleck_share(large, 240);
    REQUIRE(a > 0.01f);
    CHECK(b > a * 0.6f);
    CHECK(b < a * 1.6f);
}
