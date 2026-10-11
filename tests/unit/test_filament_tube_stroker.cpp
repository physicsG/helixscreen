// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "filament_tube_stroker.h"
#include "theme_manager.h"

#include "../catch_amalgamated.hpp"

using namespace helix::ui;

namespace {

const lv_color_t BG = lv_color_hex(0x000000);
const lv_color_t WALL = lv_color_hex(0x2196F3);
const lv_color_t BORE = lv_color_hex(0xFF0000);
const lv_color_t IDLE = lv_color_hex(0x808080);

LaneStyle style(bool halo, int32_t gauge = 5) {
    return {WALL, BORE, BG, gauge, halo};
}

bool same(lv_color_t a, lv_color_t b) {
    return lv_color_eq(a, b);
}

} // namespace

TEST_CASE("Halo layer paints two opaque pre-blended bands", "[filament-path][stroker]") {
    TubePass out[4];
    REQUIRE(build_passes(style(true), TubeLayer::Halo, out, false) == 2);
    CHECK(out[0].width == 11);
    CHECK(out[1].width == 8);
    CHECK(out[0].opa == LV_OPA_COVER);
    CHECK(out[1].opa == LV_OPA_COVER);
    CHECK(same(out[0].color, tube_blend(BG, WALL, 0.25f)));
    CHECK(same(out[1].color, tube_blend(BG, WALL, 0.55f)));
}

TEST_CASE("Halo layer is empty when simple or not requested", "[filament-path][stroker]") {
    TubePass out[4];
    CHECK(build_passes(style(true), TubeLayer::Halo, out, true) == 0);
    CHECK(build_passes(style(false), TubeLayer::Halo, out, false) == 0);
}

TEST_CASE("Wall and bore layers are one opaque pass each", "[filament-path][stroker]") {
    TubePass out[4];
    REQUIRE(build_passes(style(true), TubeLayer::Wall, out, false) == 1);
    CHECK(same(out[0].color, WALL));
    CHECK(out[0].width == 5);
    CHECK(out[0].opa == LV_OPA_COVER);

    REQUIRE(build_passes(style(true), TubeLayer::Bore, out, false) == 1);
    CHECK(same(out[0].color, BORE));
    CHECK(out[0].width == 3);
    CHECK(out[0].opa == LV_OPA_COVER);

    // simple drops only the halo
    CHECK(build_passes(style(true), TubeLayer::Wall, out, true) == 1);
    CHECK(build_passes(style(true), TubeLayer::Bore, out, true) == 1);

    REQUIRE(build_passes(style(false, 3), TubeLayer::Bore, out, false) == 1);
    CHECK(out[0].width == 1);
}

TEST_CASE("lane_style maps load and route state to walls, bore and halo",
          "[filament-path][stroker]") {
    const lv_color_t fill = BORE;
    const lv_color_t accent = WALL;

    SECTION("empty idle lane: idle walls, background bore, no halo") {
        LaneStyle st = lane_style(false, false, fill, IDLE, accent, BG, 5);
        CHECK(same(st.wall, IDLE));
        CHECK(same(st.bore, BG));
        CHECK(same(st.bg, BG));
        CHECK(st.width == 5);
        CHECK_FALSE(st.halo);
    }
    SECTION("loaded lane off the route: idle walls, filament bore") {
        LaneStyle st = lane_style(true, false, fill, IDLE, accent, BG, 5);
        CHECK(same(st.wall, IDLE));
        CHECK(same(st.bore, fill));
        CHECK_FALSE(st.halo);
    }
    SECTION("loaded lane on the active route: accent walls, filament bore, halo") {
        LaneStyle st = lane_style(true, true, fill, IDLE, accent, BG, 5);
        CHECK(same(st.wall, accent));
        CHECK(same(st.bore, fill));
        CHECK(st.halo);
    }
    SECTION("active but empty reads as idle") {
        LaneStyle st = lane_style(false, true, fill, IDLE, accent, BG, 5);
        CHECK(same(st.wall, IDLE));
        CHECK(same(st.bore, BG));
        CHECK_FALSE(st.halo);
    }
}

TEST_CASE("A lane paints halo, then wall, then bore", "[filament-path][stroker]") {
    TubePass out[4];
    REQUIRE(lane_passes(style(true), out, false) == 4);
    CHECK(out[0].width == 11);
    CHECK(out[1].width == 8);
    CHECK(same(out[2].color, WALL));
    CHECK(out[2].width == 5);
    CHECK(same(out[3].color, BORE));
    CHECK(out[3].width == 3);

    REQUIRE(lane_passes(style(true), out, true) == 2);
    CHECK(same(out[0].color, WALL));
    CHECK(same(out[1].color, BORE));
}

TEST_CASE_METHOD(LVGLTestFixture, "tube_accent lightens primary in light theme only",
                 "[filament-path][stroker]") {
    const bool original_dark = theme_manager_is_dark_mode();
    auto brightness = [](lv_color_t c) { return c.red + c.green + c.blue; };

    theme_manager_init(lv_display_get_default(), true);
    REQUIRE(theme_manager_is_dark_mode());
    CHECK(same(tube_accent(), theme_manager_get_color("primary")));

    theme_manager_init(lv_display_get_default(), false);
    REQUIRE_FALSE(theme_manager_is_dark_mode());
    const lv_color_t primary = theme_manager_get_color("primary");
    const lv_color_t accent = tube_accent();
    CHECK_FALSE(same(accent, primary));
    CHECK(brightness(accent) > brightness(primary));

    theme_manager_init(lv_display_get_default(), original_dark);
}
