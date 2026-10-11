// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ui_swatch.cpp
 * @brief Unit tests for helix::ui::apply_swatch_color (include/ui_swatch.h).
 *
 * Exercises the observable style + event-callback contract of the flat swatch
 * painter. Pixel output of the multi-color DRAW_MAIN path is NOT observable in a
 * headless test, so these tests assert the state the caller can query:
 *   - single-color  -> solid object background (bg_opa COVER, bg_color = primary)
 *   - multi-color   -> suppressed object background (bg_opa TRANSP) + a DRAW_MAIN
 *                      callback installed (event count grows by 2)
 *   - multi -> single on the same object detaches the callbacks again
 *   - repeated identical multi applies do not stack callbacks
 *   - nullptr is a safe no-op
 *   - whitespace / mixed '#'-prefixing parses correctly
 */

#include "ui_swatch.h"

#include "../lvgl_test_fixture.h"

#include "../catch_amalgamated.hpp"

namespace {

constexpr lv_opa_t SWATCH_BORDER_OPA = 80; // must match src/ui/ui_swatch.cpp

// TEST_MIRROR_OK: color_eq() wraps lv_color_eq() for assertions, as theme_live_recolor.cpp's does
// for its own use
bool color_eq(lv_color_t a, lv_color_t b) {
    return lv_color_eq(a, b);
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "apply_swatch_color: empty list yields solid fill",
                 "[ui_swatch][swatch]") {
    helix::ui::reset_swatch_state();
    lv_obj_t* sw = lv_obj_create(test_screen());
    REQUIRE(sw != nullptr);

    helix::ui::apply_swatch_color(sw, 0x123456, "");

    CHECK(lv_obj_get_style_bg_opa(sw, LV_PART_MAIN) == LV_OPA_COVER);
    CHECK(color_eq(lv_obj_get_style_bg_color(sw, LV_PART_MAIN), lv_color_hex(0x123456)));
    // Delineating border kept so a fill matching the backdrop stays visible.
    CHECK(lv_obj_get_style_border_opa(sw, LV_PART_MAIN) == SWATCH_BORDER_OPA);

    helix::ui::reset_swatch_state();
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "apply_swatch_color: single parseable color still solid (uses primary)",
                 "[ui_swatch][swatch]") {
    helix::ui::reset_swatch_state();
    lv_obj_t* sw = lv_obj_create(test_screen());

    // One parseable color => colors.size() < 2 => solid path; fill is primary_rgb,
    // NOT the single listed color.
    helix::ui::apply_swatch_color(sw, 0x00FF00, "#AABBCC");

    CHECK(lv_obj_get_style_bg_opa(sw, LV_PART_MAIN) == LV_OPA_COVER);
    CHECK(color_eq(lv_obj_get_style_bg_color(sw, LV_PART_MAIN), lv_color_hex(0x00FF00)));

    helix::ui::reset_swatch_state();
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "apply_swatch_color: two colors suppress bg and install draw callback",
                 "[ui_swatch][swatch]") {
    helix::ui::reset_swatch_state();
    lv_obj_t* sw = lv_obj_create(test_screen());

    uint32_t baseline = lv_obj_get_event_count(sw);

    helix::ui::apply_swatch_color(sw, 0x000000, "#202020,#F0F0F0");

    // Object's own background suppressed; the DRAW_MAIN callback paints the chunks.
    CHECK(lv_obj_get_style_bg_opa(sw, LV_PART_MAIN) == LV_OPA_TRANSP);
    // Two callbacks added: DRAW_MAIN painter + DELETE cleanup.
    CHECK(lv_obj_get_event_count(sw) == baseline + 2);
    // Delineating border kept.
    CHECK(lv_obj_get_style_border_opa(sw, LV_PART_MAIN) == SWATCH_BORDER_OPA);

    helix::ui::reset_swatch_state();
}

TEST_CASE_METHOD(LVGLTestFixture, "apply_swatch_color: multi -> single detaches callbacks",
                 "[ui_swatch][swatch]") {
    helix::ui::reset_swatch_state();
    lv_obj_t* sw = lv_obj_create(test_screen());

    uint32_t baseline = lv_obj_get_event_count(sw);

    helix::ui::apply_swatch_color(sw, 0x000000, "#202020,#F0F0F0");
    REQUIRE(lv_obj_get_style_bg_opa(sw, LV_PART_MAIN) == LV_OPA_TRANSP);
    REQUIRE(lv_obj_get_event_count(sw) == baseline + 2);

    // Transition back to a single color on the SAME object.
    helix::ui::apply_swatch_color(sw, 0x445566, "");

    CHECK(lv_obj_get_style_bg_opa(sw, LV_PART_MAIN) == LV_OPA_COVER);
    CHECK(color_eq(lv_obj_get_style_bg_color(sw, LV_PART_MAIN), lv_color_hex(0x445566)));
    // Both multi-color callbacks removed -> back to baseline.
    CHECK(lv_obj_get_event_count(sw) == baseline);

    helix::ui::reset_swatch_state();
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "apply_swatch_color: repeated identical multi does not stack callbacks",
                 "[ui_swatch][swatch]") {
    helix::ui::reset_swatch_state();
    lv_obj_t* sw = lv_obj_create(test_screen());

    uint32_t baseline = lv_obj_get_event_count(sw);

    helix::ui::apply_swatch_color(sw, 0x000000, "#202020,#F0F0F0");
    uint32_t after_first = lv_obj_get_event_count(sw);
    REQUIRE(after_first == baseline + 2);

    // Re-apply the same (and a different) multi-color list several times: the
    // per-object callbacks must be installed exactly once.
    helix::ui::apply_swatch_color(sw, 0x000000, "#202020,#F0F0F0");
    helix::ui::apply_swatch_color(sw, 0x000000, "#101010,#808080,#FFFFFF");
    helix::ui::apply_swatch_color(sw, 0x000000, "#202020,#F0F0F0");

    CHECK(lv_obj_get_event_count(sw) == after_first);
    CHECK(lv_obj_get_style_bg_opa(sw, LV_PART_MAIN) == LV_OPA_TRANSP);

    helix::ui::reset_swatch_state();
}

TEST_CASE_METHOD(LVGLTestFixture, "apply_swatch_color: nullptr is a safe no-op",
                 "[ui_swatch][swatch]") {
    helix::ui::reset_swatch_state();
    // Must not crash / dereference.
    helix::ui::apply_swatch_color(nullptr, 0x123456, "");
    helix::ui::apply_swatch_color(nullptr, 0x123456, "#202020,#F0F0F0");
    SUCCEED("apply_swatch_color(nullptr, ...) did not crash");
    helix::ui::reset_swatch_state();
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "apply_swatch_color: whitespace and mixed '#' prefixing parse as multi",
                 "[ui_swatch][swatch]") {
    helix::ui::reset_swatch_state();
    lv_obj_t* sw = lv_obj_create(test_screen());

    uint32_t baseline = lv_obj_get_event_count(sw);

    // " #FF0000 , 00FF00 " => two parseable colors (one '#'-prefixed, one bare,
    // both padded with whitespace) => multi-color path.
    helix::ui::apply_swatch_color(sw, 0x000000, " #FF0000 , 00FF00 ");

    CHECK(lv_obj_get_style_bg_opa(sw, LV_PART_MAIN) == LV_OPA_TRANSP);
    CHECK(lv_obj_get_event_count(sw) == baseline + 2);

    helix::ui::reset_swatch_state();
}
