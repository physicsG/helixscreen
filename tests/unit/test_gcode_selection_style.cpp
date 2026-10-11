// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../include/gcode_selection_style.h"

#include <fstream>
#include <iterator>
#include <string>

#include "../catch_amalgamated.hpp"

using namespace helix::gcode;

// ---------------------------------------------------------------------------
// resolve(): the single answer to "how does a selected/excluded segment look",
// shared by every renderer. These cases pin that answer.
// ---------------------------------------------------------------------------

TEST_CASE("plain extrusion keeps the caller's color at full opacity", "[gcode_selection_style]") {
    auto s = selection::resolve(/*excluded=*/false, /*highlighted=*/false, /*is_extrusion=*/true);
    REQUIRE(s.grey == false);
    REQUIRE(s.opa == 255);
    REQUIRE(s.tagged == false);
}

TEST_CASE("excluded segments go grey and carry the hatch tag", "[gcode_selection_style]") {
    auto s = selection::resolve(true, false, true);
    REQUIRE(s.grey == true);
    REQUIRE(s.tagged == true);
    // The tag IS the opacity byte. Anything else and stroke_exclusion_hatch()
    // has nothing to find.
    REQUIRE(s.opa == kExcludedAlpha);
}

TEST_CASE("an excluded travel is still greyed and tagged", "[gcode_selection_style]") {
    auto s = selection::resolve(true, false, /*is_extrusion=*/false);
    REQUIRE(s.grey == true);
    REQUIRE(s.opa == kExcludedAlpha);
}

// A selected object keeps its filament color and is marked by the rim instead.
TEST_CASE("highlighted segments keep filament color and carry the tag", "[gcode_selection_style]") {
    auto s = selection::resolve(false, true, true);
    REQUIRE(s.grey == false);
    REQUIRE(s.tagged == true);
    REQUIRE(s.opa == kSelectedAlpha);
}

// You have to see which object you picked in order to un-exclude it, so the
// selection tag wins the alpha byte; the body stays grey.
TEST_CASE("an excluded object that is also selected stays grey and takes the selection tag",
          "[gcode_selection_style]") {
    auto s = selection::resolve(true, true, true);
    REQUIRE(s.grey == true);
    REQUIRE(s.tagged == true);
    REQUIRE(s.opa == kSelectedAlpha);
}

TEST_CASE("excluded_grey drops the hue and keeps the shading", "[gcode_selection_style]") {
    const uint32_t bright = selection::excluded_grey(0x00C8FF);
    const uint32_t dark = selection::excluded_grey(0x003240);
    for (uint32_t g : {bright, dark}) {
        CAPTURE(g);
        REQUIRE(((g >> 16) & 0xFF) == (g & 0xFF));
        REQUIRE(((g >> 8) & 0xFF) == (g & 0xFF));
    }
    REQUIRE((bright & 0xFF) > (dark & 0xFF));
    // The floor keeps black filament off the dark background.
    REQUIRE((selection::excluded_grey(0x000000) & 0xFF) ==
            static_cast<uint32_t>(selection::kExcludedGreyFloor));
}

TEST_CASE("travel moves are never tagged", "[gcode_selection_style]") {
    // A travel move belonging to the selected object must not contribute to the
    // silhouette: travels cut across the interior, so tagging them would drag the
    // tagged region out to a bounding box and the rim would trace that instead of
    // the object.
    auto s = selection::resolve(false, true, /*is_extrusion=*/false);
    REQUIRE(s.tagged == false);
    REQUIRE(s.opa != kSelectedAlpha);
}

// ---------------------------------------------------------------------------
// outline_width_px(): the rim is measured in SCREEN PIXELS by both renderers.
//
// A world-space width cannot hold a visible rim: 0.25mm at the plate-wide zoom
// the viewer opens on is about half a pixel, which reads as speckle or nothing.
// ---------------------------------------------------------------------------

TEST_CASE("the rim is at least one pixel on any panel", "[gcode_selection_style]") {
    for (int w : {128, 320, 321, 480, 800, 1024, 1920}) {
        REQUIRE(selection::outline_width_px(w) >= 1);
    }
}

TEST_CASE("the rim is thinner on small panels", "[gcode_selection_style]") {
    // At 480x272 a 2px-per-side rim swallows small objects whole.
    REQUIRE(selection::outline_width_px(320) < selection::outline_width_px(800));
    // And the boundary is inclusive, so 320 itself counts as small.
    REQUIRE(selection::outline_width_px(selection::kSmallPanelWidthPx) ==
            selection::kOutlineSmallPanelPx);
    REQUIRE(selection::outline_width_px(selection::kSmallPanelWidthPx + 1) ==
            selection::kOutlinePx);
}

// outline_width_px_scaled(): the GLES renderer strokes the rim on its readback,
// which supersampled stills render at 2x and moving frames at half resolution.
// The widget decides the width; the ratio only converts it into readback px.
TEST_CASE("the scaled rim converts the widget rim at any readback ratio",
          "[gcode_selection_style]") {
    // A 2x supersampled still strokes 2 readback px per screen px of rim.
    REQUIRE(selection::outline_width_px_scaled(368, 736) == 2 * selection::outline_width_px(368));
    // A widget-sized readback is the plain widget rim.
    REQUIRE(selection::outline_width_px_scaled(390, 390) == selection::outline_width_px(390));
}

TEST_CASE("a half-resolution readback never rounds the rim away", "[gcode_selection_style]") {
    // An odd small-panel widget at half res: the 1px rim times 159/319 is 0.498,
    // which rounds to 0, and stroke_selection_rim ignores a rim below 1px.
    REQUIRE(selection::outline_width_px_scaled(319, 159) >= 1);
}

// ---------------------------------------------------------------------------
// bracket_arm_length(): the one arm length both renderers draw corner brackets with.
// ---------------------------------------------------------------------------

TEST_CASE("bracket arm is 20% of the shortest edge", "[gcode_selection_style]") {
    AABB bbox{glm::vec3(0.0f), glm::vec3(20.0f, 30.0f, 40.0f)};
    REQUIRE(selection::bracket_arm_length(bbox) == Catch::Approx(4.0f)); // 20 * 0.2
}

TEST_CASE("bracket arm is capped at 5mm on large objects", "[gcode_selection_style]") {
    AABB bbox{glm::vec3(0.0f), glm::vec3(200.0f, 200.0f, 200.0f)};
    REQUIRE(selection::bracket_arm_length(bbox) == Catch::Approx(5.0f));
}

TEST_CASE("a degenerate bbox yields no arm", "[gcode_selection_style]") {
    // Below 0.01mm the brackets are sub-pixel noise, so neither renderer draws
    // them. 0 is the shared "do not draw" signal.
    AABB bbox{glm::vec3(0.0f), glm::vec3(0.001f, 0.001f, 0.001f)};
    REQUIRE(selection::bracket_arm_length(bbox) == 0.0f);
}

// An empty AABB has infinite edges, so the helper must reject it explicitly
// rather than rely on the degeneracy guard tripping on -inf.
TEST_CASE("an empty bbox yields no arm rather than inf or nan", "[gcode_selection_style]") {
    AABB empty;
    REQUIRE(empty.is_empty());
    REQUIRE(selection::bracket_arm_length(empty) == 0.0f);
}

// ---------------------------------------------------------------------------
// to_vec4(): the GLES path needs the same constants as normalized floats, exact
// to the byte. 0.75 * 255 = 191, so a hand-rounded 0.75f is not 0xC0 (192).
// ---------------------------------------------------------------------------

TEST_CASE("to_vec4 round-trips the bracket color exactly", "[gcode_selection_style]") {
    // The SHIPPED bracket color, not the sentinel: this case is about the exact
    // float conversion of 0xC0C0C0.
    auto v = selection::to_vec4(selection::Palette{}.bracket);
    REQUIRE(v.r == Catch::Approx(192.0f / 255.0f));
    REQUIRE(v.g == Catch::Approx(192.0f / 255.0f));
    REQUIRE(v.b == Catch::Approx(192.0f / 255.0f));
    REQUIRE(v.a == Catch::Approx(1.0f));
    // 0.75f is the nearby approximation that renders 191 instead of 192.
    REQUIRE(v.r != Catch::Approx(0.75f));
}

TEST_CASE("to_vec4 carries alpha through", "[gcode_selection_style]") {
    auto v = selection::to_vec4(selection::Palette{}.excluded, 153);
    REQUIRE(v.a == Catch::Approx(153.0f / 255.0f));
}

// ---------------------------------------------------------------------------
// The compiled defaults and the XML tokens are two statements of one value.
//
// selection::Palette carries defaults only so the headless fixtures render the
// right hues: LVGLTestFixture starts LVGL without theme_manager, so no token is
// registered for palette_from_theme() to read. Shipped trees always have them.
//
// That still makes the defaults a second copy of a number whose real home is
// ui_xml/gcode_tokens.xml, and two hand-written copies of one value agree by
// convention until they silently do not. This reads the token file and fails
// when they drift.
//
// Parsed with a plain scan rather than the XML engine on purpose: the point is
// to check the bytes a human edits, without needing LVGL, a display, or the
// theme registered.
// ---------------------------------------------------------------------------

TEST_CASE("Palette defaults match ui_xml/gcode_tokens.xml", "[gcode_selection_style][tokens]") {
    std::ifstream f("ui_xml/gcode_tokens.xml");
    REQUIRE(f.is_open()); // helix-tests runs from the repo root

    std::string xml((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    auto token_value = [&](const std::string& name) -> uint32_t {
        const std::string key = "<color name=\"" + name + "\" value=\"#";
        const size_t at = xml.find(key);
        REQUIRE(at != std::string::npos);
        return static_cast<uint32_t>(std::stoul(xml.substr(at + key.size(), 6), nullptr, 16));
    };

    const selection::Palette defaults;
    CHECK(token_value("gcode_selection_outline") == defaults.outline);
    CHECK(token_value("gcode_selection_excluded") == defaults.excluded);
    CHECK(token_value("gcode_selection_bracket") == defaults.bracket);
}
