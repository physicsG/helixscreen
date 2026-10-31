// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "glass_shading.h"

#include <cstdint>
#include <cstdlib>

#include "../catch_amalgamated.hpp"

using helix::ui::glass_rim_color;
using helix::ui::glass_stops;
using helix::ui::GlassFamily;
using helix::ui::GlassStops;

namespace {

lv_color_t toward_white(lv_color_t base, int pct) {
    return lv_color_mix(lv_color_white(), base, static_cast<uint8_t>(pct * 255 / 100));
}

lv_color_t toward_black(lv_color_t base, int pct) {
    return lv_color_mix(lv_color_black(), base, static_cast<uint8_t>(pct * 255 / 100));
}

bool same(lv_color_t a, lv_color_t b) {
    return a.red == b.red && a.green == b.green && a.blue == b.blue;
}

int hue_distance(int a, int b) {
    const int d = std::abs(a - b) % 360;
    return d > 180 ? 360 - d : d;
}

} // namespace

TEST_CASE("glass_stops: raised dark matches the spec table", "[glass]") {
    const lv_color_t base = lv_color_hex(0x3A7CC8);
    const GlassStops s = glass_stops(GlassFamily::Raised, base, true, false);
    REQUIRE(s.count == 4);
    CHECK(same(s.color[0], toward_white(base, 45)));
    CHECK(same(s.color[1], toward_white(base, 22)));
    CHECK(same(s.color[2], toward_white(base, 6)));
    CHECK(same(s.color[3], toward_black(base, 22)));
    CHECK(s.frac[0] == 0);
    CHECK(s.frac[1] == 23);
    CHECK(s.frac[2] == 102);
    CHECK(s.frac[3] == 255);
}

TEST_CASE("glass_stops: raised light, surface and recessed match the spec table", "[glass]") {
    const lv_color_t base = lv_color_hex(0x3A7CC8);

    const GlassStops raised = glass_stops(GlassFamily::Raised, base, false, false);
    REQUIRE(raised.count == 4);
    CHECK(same(raised.color[0], toward_white(base, 50)));
    CHECK(same(raised.color[3], toward_black(base, 18)));

    const GlassStops surface_dark = glass_stops(GlassFamily::Surface, base, true, false);
    REQUIRE(surface_dark.count == 4);
    CHECK(same(surface_dark.color[0], toward_white(base, 24)));
    CHECK(same(surface_dark.color[1], toward_white(base, 10)));
    CHECK(same(surface_dark.color[2], base));
    CHECK(same(surface_dark.color[3], toward_black(base, 12)));
    CHECK(surface_dark.frac[1] == 70);
    CHECK(surface_dark.frac[2] == 160);

    const GlassStops surface_light = glass_stops(GlassFamily::Surface, base, false, false);
    REQUIRE(surface_light.count == 3);
    CHECK(same(surface_light.color[0], base));
    CHECK(same(surface_light.color[1], toward_black(base, 3)));
    CHECK(same(surface_light.color[2], toward_black(base, 9)));

    const GlassStops recessed = glass_stops(GlassFamily::Recessed, base, true, false);
    REQUIRE(recessed.count == 2);
    CHECK(same(recessed.color[0], toward_black(base, 30)));
    CHECK(same(recessed.color[1], base));
    CHECK(same(glass_stops(GlassFamily::Recessed, base, false, false).color[0],
               toward_black(base, 12)));
}

TEST_CASE("glass_stops: pressed raised darkens top to bottom", "[glass]") {
    const lv_color_t base = lv_color_hex(0x3A7CC8);
    for (bool dark : {true, false}) {
        CAPTURE(dark);
        const GlassStops s = glass_stops(GlassFamily::Raised, base, dark, true);
        REQUIRE(s.count == 2);
        CHECK(lv_color_luminance(s.color[0]) < lv_color_luminance(s.color[1]));
        CHECK(same(s.color[0], toward_black(base, 25)));
        CHECK(same(s.color[1], toward_black(base, 5)));
        CHECK(s.frac[0] == 0);
        CHECK(s.frac[1] == 255);
    }
}

TEST_CASE("glass_stops: no hue shift", "[glass]") {
    for (uint32_t hex : {0x3A7CC8u, 0xD94848u, 0x5CB85Cu, 0xE8A83Au, 0x7C6CC8u}) {
        const lv_color_t base = lv_color_hex(hex);
        const lv_color_hsv_t base_hsv = lv_color_to_hsv(base);
        if (base_hsv.s < 10) {
            continue;
        }
        for (GlassFamily f : {GlassFamily::Raised, GlassFamily::Surface, GlassFamily::Recessed}) {
            for (bool dark : {true, false}) {
                for (bool pressed : {false, true}) {
                    const GlassStops s = glass_stops(f, base, dark, pressed);
                    for (int i = 0; i < s.count; ++i) {
                        CAPTURE(hex, static_cast<int>(f), dark, pressed, i);
                        const lv_color_hsv_t hsv = lv_color_to_hsv(s.color[i]);
                        CHECK(hue_distance(hsv.h, base_hsv.h) <= 4);
                    }
                }
            }
        }
    }
}

TEST_CASE("glass_stops: none has no stops", "[glass]") {
    const lv_color_t base = lv_color_hex(0x3A7CC8);
    CHECK(glass_stops(GlassFamily::None, base, true, false).count == 0);
    CHECK(glass_stops(GlassFamily::None, base, false, true).count == 0);
    CHECK(same(glass_rim_color(GlassFamily::None, base, true), base));
}

TEST_CASE("glass_rim_color: lighter in dark, darker in light", "[glass]") {
    const lv_color_t base = lv_color_hex(0x3A7CC8);
    const uint8_t lum = lv_color_luminance(base);
    CHECK(lv_color_luminance(glass_rim_color(GlassFamily::Raised, base, true)) > lum);
    CHECK(lv_color_luminance(glass_rim_color(GlassFamily::Raised, base, false)) < lum);
    CHECK(same(glass_rim_color(GlassFamily::Raised, base, true), toward_white(base, 40)));
    CHECK(same(glass_rim_color(GlassFamily::Raised, base, false), toward_black(base, 22)));
    CHECK(same(glass_rim_color(GlassFamily::Surface, base, true), toward_white(base, 12)));
    CHECK(same(glass_rim_color(GlassFamily::Surface, base, false), toward_black(base, 14)));
    CHECK(same(glass_rim_color(GlassFamily::Recessed, base, true), toward_black(base, 40)));
    CHECK(same(glass_rim_color(GlassFamily::Recessed, base, false), toward_black(base, 20)));
}

TEST_CASE("glass_gradient: vertical for raised and recessed, radial from top-left for surface",
          "[glass]") {
    const lv_color_t base = lv_color_hex(0x3A7CC8);

    lv_grad_dsc_t raised{};
    REQUIRE(helix::ui::glass_gradient(&raised, GlassFamily::Raised, base, true, false));
    CHECK(static_cast<int>(raised.dir) == LV_GRAD_DIR_VER);
    REQUIRE(raised.stops_count == 4);
    CHECK(same(raised.stops[0].color, toward_white(base, 45)));
    CHECK(raised.stops[2].frac == 102);
    CHECK(raised.stops[3].opa == LV_OPA_COVER);

    lv_grad_dsc_t recessed{};
    REQUIRE(helix::ui::glass_gradient(&recessed, GlassFamily::Recessed, base, true, false));
    CHECK(static_cast<int>(recessed.dir) == LV_GRAD_DIR_VER);

    lv_grad_dsc_t surface{};
    REQUIRE(helix::ui::glass_gradient(&surface, GlassFamily::Surface, base, true, false));
    CHECK(static_cast<int>(surface.dir) == LV_GRAD_DIR_RADIAL);
    CHECK(surface.params.radial.end.x == 0);
    CHECK(surface.params.radial.end.y == 0);
    CHECK(surface.params.radial.end_extent.x == helix::ui::kGlassGlowRadiusPx);
    CHECK(surface.params.radial.end_extent.y == 0);
    CHECK(static_cast<int>(surface.extend) == LV_GRAD_EXTEND_PAD);

    lv_grad_dsc_t none{};
    none.dir = LV_GRAD_DIR_NONE;
    CHECK_FALSE(helix::ui::glass_gradient(&none, GlassFamily::None, base, true, false));
    CHECK(static_cast<int>(none.dir) == LV_GRAD_DIR_NONE);
    CHECK(none.stops_count == 0);
}
