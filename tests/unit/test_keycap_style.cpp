// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_keycap_style.cpp
 * @brief Unit tests for on-screen keyboard key depth (ui_keycap_style.h)
 *
 * Test-first: written before the implementation.
 *
 * Covers the two decisions the module makes — which treatment the hardware can
 * afford, and how deep the theme wants the keycap — plus the colour derivation
 * that has to hold in both light and dark mode.
 */

#include "../../include/ui_keycap_style.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::ui;

namespace {

/// Perceived brightness, for asserting "darker than" without pinning exact hex.
int luma(lv_color_t c) {
    return (c.red * 299 + c.green * 587 + c.blue * 114) / 1000;
}

/// A light-mode key background (elevated_bg from a light theme).
constexpr lv_color_t LIGHT_KEY = {0xF0, 0xF0, 0xF0}; // blue, green, red

/// A dark-mode key background (elevated_bg from catppuccin-style dark).
constexpr lv_color_t DARK_KEY = {0x44, 0x32, 0x31};

} // namespace

// ============================================================================
// Hardware gate: decide_keycap_depth()
// ============================================================================

TEST_CASE("Keycap depth: constrained hardware gets the borderless emboss", "[keycap][depth]") {
    // EMBEDDED is < 512MB RAM or single core (AD5M, CC1). 30+ software-rendered
    // box shadows per matrix repaint is the cost this avoids.
    REQUIRE(decide_keycap_depth(PlatformTier::EMBEDDED, true) == KeycapDepth::EMBOSS);
}

TEST_CASE("Keycap depth: capable hardware gets the skirt", "[keycap][depth]") {
    REQUIRE(decide_keycap_depth(PlatformTier::BASIC, true) == KeycapDepth::SKIRT);
    REQUIRE(decide_keycap_depth(PlatformTier::STANDARD, true) == KeycapDepth::SKIRT);
}

TEST_CASE("Keycap depth: builds without shadow rendering stay flat", "[keycap][depth]") {
    // ESP32 disables key shadows outright and strips pressed-state styles; even
    // the emboss border is a per-key draw op there.
    REQUIRE(decide_keycap_depth(PlatformTier::STANDARD, false) == KeycapDepth::FLAT);
    REQUIRE(decide_keycap_depth(PlatformTier::BASIC, false) == KeycapDepth::FLAT);
    REQUIRE(decide_keycap_depth(PlatformTier::EMBEDDED, false) == KeycapDepth::FLAT);
}

// ============================================================================
// Theme depth: keycap_skirt_offset_px()
// ============================================================================

TEST_CASE("Skirt depth: shadow_intensity 0 means default, not off", "[keycap][depth]") {
    // 16 of the 18 shipped themes leave shadow_intensity at 0. Reading that as
    // "flat" would hide the keycap from almost every user; theme_manager.cpp
    // already reads it as "default" for slider knobs.
    REQUIRE(keycap_skirt_offset_px(0) == KEYCAP_DEFAULT_SKIRT_PX);
    REQUIRE(KEYCAP_DEFAULT_SKIRT_PX >= 1);
}

TEST_CASE("Skirt depth: a theme's shadow_intensity scales the rim", "[keycap][depth]") {
    REQUIRE(keycap_skirt_offset_px(8) == 1);  // cupertino
    REQUIRE(keycap_skirt_offset_px(30) == 3); // chatgpt
    REQUIRE(keycap_skirt_offset_px(20) == 2);
}

TEST_CASE("Skirt depth: clamped to a rim, never a slab", "[keycap][depth]") {
    // The editor slider goes to 50, which as a raw blur width would swallow the
    // gap between key rows.
    REQUIRE(keycap_skirt_offset_px(50) == KEYCAP_MAX_SKIRT_PX);
    REQUIRE(keycap_skirt_offset_px(255) == KEYCAP_MAX_SKIRT_PX);
}

TEST_CASE("Skirt depth: an enabled skirt is never invisible", "[keycap][depth]") {
    // A 0px offset with a 1px blur draws nothing a user can see, so the low end
    // has to floor at 1 rather than round to 0.
    for (int intensity = 1; intensity <= 50; ++intensity) {
        REQUIRE(keycap_skirt_offset_px(intensity) >= 1);
    }
    REQUIRE(keycap_skirt_offset_px(-5) == KEYCAP_DEFAULT_SKIRT_PX);
}

// ============================================================================
// Colour derivation: make_keycap_style()
// ============================================================================

TEST_CASE("Keycap style: the skirt is a darker edge of the key itself", "[keycap][style]") {
    // Derived from the key background, not the theme's black cast shadow — a
    // keycap rim is the side wall of the key, not a shadow it throws.
    const auto light = make_keycap_style(KeycapDepth::SKIRT, LIGHT_KEY, false, 0);
    REQUIRE(light.depth == KeycapDepth::SKIRT);
    REQUIRE(luma(light.skirt_color) < luma(LIGHT_KEY));
    REQUIRE(light.skirt_opa > LV_OPA_TRANSP);
    REQUIRE(light.skirt_offset_y == KEYCAP_DEFAULT_SKIRT_PX);

    const auto dark = make_keycap_style(KeycapDepth::SKIRT, DARK_KEY, true, 0);
    REQUIRE(luma(dark.skirt_color) < luma(DARK_KEY));
    REQUIRE(dark.skirt_opa > LV_OPA_TRANSP);
}

TEST_CASE("Keycap style: a dark theme's rim has to clear its own background", "[keycap][style]") {
    // A dark key sits on an even darker keyboard background, so the rim only
    // reads if it is pushed well below the key — a light-mode darkening amount
    // would land on the background colour and vanish.
    const auto dark = make_keycap_style(KeycapDepth::SKIRT, DARK_KEY, true, 0);
    const auto light = make_keycap_style(KeycapDepth::SKIRT, DARK_KEY, false, 0);
    REQUIRE(luma(dark.skirt_color) < luma(light.skirt_color));
}

TEST_CASE("Keycap style: a dark theme's key is lit from the top, not shaded below",
          "[keycap][style]") {
    // The skirt is drawn under the key, on the keyboard background, and a dark
    // theme's background is darker than the key — measured on a real dark theme
    // the skirt landed within four values of it. A DARKER edge would be more of
    // the same; the cue that reads is the lit top of a raised cap.
    const auto dark = make_keycap_style(KeycapDepth::SKIRT, DARK_KEY, true, 0);
    REQUIRE(dark.edge_width > 0);
    REQUIRE(luma(dark.edge_color) > luma(DARK_KEY));
    REQUIRE((dark.edge_side & LV_BORDER_SIDE_TOP) != 0);
    REQUIRE((dark.edge_side & LV_BORDER_SIDE_BOTTOM) == 0);

    // Light mode needs no such help: the rim contrasts against a lighter
    // background on its own, and an edge would only muddy the key face.
    const auto light = make_keycap_style(KeycapDepth::SKIRT, LIGHT_KEY, false, 0);
    REQUIRE(light.edge_width == 0);
}

TEST_CASE("Keycap style: the light moves when the key is held", "[keycap][style]") {
    // A held key sinks, so whichever corner the light was on has to swap.
    for (bool dark : {false, true}) {
        const auto style =
            make_keycap_style(KeycapDepth::EMBOSS, dark ? DARK_KEY : LIGHT_KEY, dark, 0);
        INFO("dark = " << dark);
        REQUIRE(style.edge_side != style.edge_side_pressed);
        REQUIRE((style.edge_side & style.edge_side_pressed) == 0);
    }
}

TEST_CASE("Keycap style: the theme's depth reaches the skirt", "[keycap][style]") {
    const auto shallow = make_keycap_style(KeycapDepth::SKIRT, LIGHT_KEY, false, 8);
    const auto deep = make_keycap_style(KeycapDepth::SKIRT, LIGHT_KEY, false, 30);
    REQUIRE(shallow.skirt_offset_y < deep.skirt_offset_y);
    REQUIRE(deep.skirt_offset_y == keycap_skirt_offset_px(30));
}

TEST_CASE("Keycap style: emboss costs no shadow at all", "[keycap][style]") {
    // The whole point of the emboss branch is that constrained hardware draws
    // zero box shadows. A non-zero opacity here would put the cost straight back.
    const auto style = make_keycap_style(KeycapDepth::EMBOSS, LIGHT_KEY, false, 0);
    REQUIRE(style.depth == KeycapDepth::EMBOSS);
    REQUIRE(style.skirt_opa == LV_OPA_TRANSP);
    REQUIRE(style.skirt_offset_y == 0);
    REQUIRE(style.edge_width > 0);
    REQUIRE(luma(style.edge_color) < luma(LIGHT_KEY)); // light theme: shaded underside
    REQUIRE((style.edge_side & LV_BORDER_SIDE_BOTTOM) != 0);
}

TEST_CASE("Keycap style: flat carries neither treatment", "[keycap][style]") {
    const auto style = make_keycap_style(KeycapDepth::FLAT, LIGHT_KEY, false, 30);
    REQUIRE(style.skirt_opa == LV_OPA_TRANSP);
    REQUIRE(style.skirt_offset_y == 0);
    REQUIRE(style.edge_width == 0);
}

TEST_CASE("Keycap style: every depth still gets a press cue", "[keycap][style]") {
    // The key cannot move: buttonmatrix computes its own button rects and hands
    // them to lv_draw_rect, so translate_y on LV_PART_ITEMS does nothing. The
    // background swap is the entire press feedback.
    for (auto depth : {KeycapDepth::FLAT, KeycapDepth::EMBOSS, KeycapDepth::SKIRT}) {
        const auto light = make_keycap_style(depth, LIGHT_KEY, false, 0);
        INFO("depth = " << static_cast<int>(depth));
        REQUIRE(luma(light.pressed_bg) < luma(LIGHT_KEY)); // light mode: sinks

        const auto dark = make_keycap_style(depth, DARK_KEY, true, 0);
        REQUIRE(luma(dark.pressed_bg) > luma(DARK_KEY)); // dark mode: lifts
    }
}

// ============================================================================
// Override: parse_keycap_depth_override()
// ============================================================================

TEST_CASE("Keycap override: names every treatment", "[keycap][override]") {
    REQUIRE(parse_keycap_depth_override("skirt") == KeycapDepth::SKIRT);
    REQUIRE(parse_keycap_depth_override("emboss") == KeycapDepth::EMBOSS);
    REQUIRE(parse_keycap_depth_override("flat") == KeycapDepth::FLAT);
    REQUIRE(parse_keycap_depth_override("SKIRT") == KeycapDepth::SKIRT);
}

TEST_CASE("Keycap override: unset or unrecognised leaves the tier in charge",
          "[keycap][override]") {
    REQUIRE_FALSE(parse_keycap_depth_override(nullptr).has_value());
    REQUIRE_FALSE(parse_keycap_depth_override("").has_value());
    REQUIRE_FALSE(parse_keycap_depth_override("3d").has_value());
    REQUIRE_FALSE(parse_keycap_depth_override("1").has_value());
}
