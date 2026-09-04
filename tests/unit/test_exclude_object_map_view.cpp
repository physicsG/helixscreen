// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_exclude_object_map_view.cpp
 * @brief Tests for the exclude-object overhead map view feature
 *
 * Task 1: Verifies that object color palette tokens (object_color_1 through
 * object_color_8) are registered and return non-black colors after theme init.
 *
 * Uses XMLTestFixture because theme_manager_init() must have been called for
 * lv_xml_register_const tokens to be accessible via theme_manager_get_color().
 */

#include "../test_fixtures.h"
#include "theme_manager.h"

#include "../catch_amalgamated.hpp"

// ============================================================================
// Object color palette token tests
// ============================================================================

TEST_CASE_METHOD(XMLTestFixture, "object_color_1 token returns non-black color",
                 "[exclude_map][tokens]") {
    lv_color_t color = theme_manager_get_color("object_color_1");
    // #7c8aff — periwinkle blue: red=0x7c, non-black
    REQUIRE(color.red != 0);
}

TEST_CASE_METHOD(XMLTestFixture, "object_color_2 token returns non-black color",
                 "[exclude_map][tokens]") {
    lv_color_t color = theme_manager_get_color("object_color_2");
    // #4ecdc4 — teal: green=0xcd, non-black
    REQUIRE(color.green != 0);
}

TEST_CASE_METHOD(XMLTestFixture, "object_color_3 token returns non-black color",
                 "[exclude_map][tokens]") {
    lv_color_t color = theme_manager_get_color("object_color_3");
    // #f9c74f — golden yellow: red=0xf9, non-black
    REQUIRE(color.red != 0);
}

TEST_CASE_METHOD(XMLTestFixture, "object_color_4 token returns non-black color",
                 "[exclude_map][tokens]") {
    lv_color_t color = theme_manager_get_color("object_color_4");
    // #a78bfa — soft purple: red=0xa7, non-black
    REQUIRE(color.red != 0);
}

TEST_CASE_METHOD(XMLTestFixture, "object_color_5 token returns non-black color",
                 "[exclude_map][tokens]") {
    lv_color_t color = theme_manager_get_color("object_color_5");
    // #f472b6 — pink: red=0xf4, non-black
    REQUIRE(color.red != 0);
}

TEST_CASE_METHOD(XMLTestFixture, "object_color_6 token returns non-black color",
                 "[exclude_map][tokens]") {
    lv_color_t color = theme_manager_get_color("object_color_6");
    // #fb923c — orange: red=0xfb, non-black
    REQUIRE(color.red != 0);
}

TEST_CASE_METHOD(XMLTestFixture, "object_color_7 token returns non-black color",
                 "[exclude_map][tokens]") {
    lv_color_t color = theme_manager_get_color("object_color_7");
    // #34d399 — emerald: green=0xd3, non-black
    REQUIRE(color.green != 0);
}

TEST_CASE_METHOD(XMLTestFixture, "object_color_8 token returns non-black color",
                 "[exclude_map][tokens]") {
    lv_color_t color = theme_manager_get_color("object_color_8");
    // #60a5fa — sky blue: blue=0xfa, non-black
    REQUIRE(color.blue != 0);
}

TEST_CASE_METHOD(XMLTestFixture, "all 8 object color tokens are registered",
                 "[exclude_map][tokens]") {
    // Verify all 8 tokens return non-zero colors (not black fallback)
    lv_color_t black = lv_color_hex(0x000000);

    for (int i = 1; i <= 8; ++i) {
        char token[32];
        snprintf(token, sizeof(token), "object_color_%d", i);
        lv_color_t color = theme_manager_get_color(token);

        // At least one channel must be non-zero to distinguish from black fallback
        bool is_non_black =
            (color.red != black.red) || (color.green != black.green) || (color.blue != black.blue);
        INFO("Token " << token << " returned black (missing registration)");
        REQUIRE(is_non_black);
    }
}

// ============================================================================
// Coordinate mapping tests
// ============================================================================

#include "ui_exclude_object_map_view.h"

using helix::ui::ExcludeObjectMapView;

// ============================================================================
// KeyBarMode tests (pure logic, no LVGL widget creation needed)
// ============================================================================

TEST_CASE("key_bar_mode returns FullNames for <= 4 objects", "[exclude_map][key_bar_mode]") {
    using helix::ui::ExcludeObjectMapView;
    REQUIRE(ExcludeObjectMapView::key_bar_mode(0) == ExcludeObjectMapView::KeyBarMode::FullNames);
    REQUIRE(ExcludeObjectMapView::key_bar_mode(1) == ExcludeObjectMapView::KeyBarMode::FullNames);
    REQUIRE(ExcludeObjectMapView::key_bar_mode(4) == ExcludeObjectMapView::KeyBarMode::FullNames);
}

TEST_CASE("key_bar_mode returns Abbreviated for 5-7 objects", "[exclude_map][key_bar_mode]") {
    using helix::ui::ExcludeObjectMapView;
    REQUIRE(ExcludeObjectMapView::key_bar_mode(5) == ExcludeObjectMapView::KeyBarMode::Abbreviated);
    REQUIRE(ExcludeObjectMapView::key_bar_mode(6) == ExcludeObjectMapView::KeyBarMode::Abbreviated);
    REQUIRE(ExcludeObjectMapView::key_bar_mode(7) == ExcludeObjectMapView::KeyBarMode::Abbreviated);
}

TEST_CASE("key_bar_mode returns Summary for >= 8 objects", "[exclude_map][key_bar_mode]") {
    using helix::ui::ExcludeObjectMapView;
    REQUIRE(ExcludeObjectMapView::key_bar_mode(8) == ExcludeObjectMapView::KeyBarMode::Summary);
    REQUIRE(ExcludeObjectMapView::key_bar_mode(12) == ExcludeObjectMapView::KeyBarMode::Summary);
    REQUIRE(ExcludeObjectMapView::key_bar_mode(100) == ExcludeObjectMapView::KeyBarMode::Summary);
}

// ============================================================================
// Adaptive key bar mode selection (consolidated)
// ============================================================================

TEST_CASE("Adaptive key bar mode selection", "[exclude_map][key_bar]") {
    using KBM = ExcludeObjectMapView::KeyBarMode;
    REQUIRE(ExcludeObjectMapView::key_bar_mode(2) == KBM::FullNames);
    REQUIRE(ExcludeObjectMapView::key_bar_mode(4) == KBM::FullNames);
    REQUIRE(ExcludeObjectMapView::key_bar_mode(5) == KBM::Abbreviated);
    REQUIRE(ExcludeObjectMapView::key_bar_mode(7) == KBM::Abbreviated);
    REQUIRE(ExcludeObjectMapView::key_bar_mode(8) == KBM::Summary);
    REQUIRE(ExcludeObjectMapView::key_bar_mode(20) == KBM::Summary);
}

// ============================================================================
// Coordinate mapping tests
// ============================================================================

TEST_CASE("Coordinate mapping: mm to pixels", "[exclude_map][coords]") {
    SECTION("square bed in square viewport") {
        auto mapper = ExcludeObjectMapView::CoordMapper(235.0f, 235.0f, 400, 400);
        auto [px, py] = mapper.mm_to_px(0.0f, 0.0f);
        REQUIRE_THAT(px, Catch::Matchers::WithinAbs(0.0f, 0.5f));
        REQUIRE_THAT(py, Catch::Matchers::WithinAbs(400.0f, 0.5f));

        auto [cx, cy] = mapper.mm_to_px(117.5f, 117.5f);
        REQUIRE_THAT(cx, Catch::Matchers::WithinAbs(200.0f, 0.5f));
        REQUIRE_THAT(cy, Catch::Matchers::WithinAbs(200.0f, 0.5f));
    }

    SECTION("rectangular bed — width-limited") {
        auto mapper = ExcludeObjectMapView::CoordMapper(350.0f, 200.0f, 400, 400);
        auto [cx, cy] = mapper.mm_to_px(175.0f, 100.0f);
        REQUIRE_THAT(cx, Catch::Matchers::WithinAbs(200.0f, 0.5f));
        REQUIRE_THAT(cy, Catch::Matchers::WithinAbs(200.0f, 0.5f));
    }

    SECTION("bbox_to_rect") {
        auto mapper = ExcludeObjectMapView::CoordMapper(235.0f, 235.0f, 470, 470);
        auto rect = mapper.bbox_to_rect({10.0f, 10.0f}, {60.0f, 40.0f});
        REQUIRE_THAT(rect.w, Catch::Matchers::WithinAbs(100.0f, 0.5f));
        REQUIRE_THAT(rect.h, Catch::Matchers::WithinAbs(60.0f, 0.5f));
    }

    SECTION("minimum rect size enforced") {
        auto mapper = ExcludeObjectMapView::CoordMapper(350.0f, 350.0f, 400, 400);
        auto rect = mapper.bbox_to_rect({100.0f, 100.0f}, {101.0f, 101.0f});
        REQUIRE(rect.w >= 28.0f);
        REQUIRE(rect.h >= 28.0f);
    }
}

// ============================================================================
// Bed size fallback: derive bed extents from object bounding boxes
// ============================================================================

TEST_CASE("Bed size fallback from object extents", "[exclude_map][bed_size]") {
    // When bed dimensions are unknown (0x0), the caller derives them from the
    // union of all object bounding boxes plus a 10% padding margin.
    // Objects span 20-170mm x 30-140mm → effective extents: 170*1.1 x 140*1.1
    auto mapper = ExcludeObjectMapView::CoordMapper(170.0f * 1.1f, 140.0f * 1.1f, 400, 400);
    // Center of objects area (85, 70) should map near viewport center
    auto [cx, cy] = mapper.mm_to_px(85.0f, 70.0f);
    REQUIRE(cx > 150.0f);
    REQUIRE(cx < 250.0f);
    REQUIRE(cy > 150.0f);
    REQUIRE(cy < 250.0f);
}

// ============================================================================
// CoordMapper edge cases
// ============================================================================

TEST_CASE("CoordMapper edge cases", "[exclude_map][coords]") {
    SECTION("very narrow bed (portrait)") {
        // Height-limited: scale = 400/300 = 1.333
        auto mapper = ExcludeObjectMapView::CoordMapper(100.0f, 300.0f, 400, 400);
        auto [cx, cy] = mapper.mm_to_px(50.0f, 150.0f);
        REQUIRE_THAT(cx, Catch::Matchers::WithinAbs(200.0f, 1.0f));
        REQUIRE_THAT(cy, Catch::Matchers::WithinAbs(200.0f, 1.0f));
    }

    SECTION("overlapping minimum-size rects") {
        // Two tiny adjacent objects (1mm each) on a 235mm square bed.
        // Both must be expanded to at least MIN_TOUCH_TARGET_PX.
        auto mapper = ExcludeObjectMapView::CoordMapper(235.0f, 235.0f, 400, 400);
        auto r1 = mapper.bbox_to_rect({100.0f, 100.0f}, {101.0f, 101.0f});
        auto r2 = mapper.bbox_to_rect({102.0f, 100.0f}, {103.0f, 101.0f});
        REQUIRE(r1.w >= ExcludeObjectMapView::MIN_TOUCH_TARGET_PX);
        REQUIRE(r2.w >= ExcludeObjectMapView::MIN_TOUCH_TARGET_PX);
    }
}

// ============================================================================
// create()/destroy() lifecycle — canvas-buffer ordering + deferred deletion
// ============================================================================
//
// Regression for the LVGL event-list-corruption bug: destroy() must NOT call a
// bare synchronous lv_obj_delete(root_) (it can run inside a UpdateQueue
// process_pending batch via the memory-pressure reclaim chain, where a second
// sync deletion corrupts LVGL's global event linked list). It defers deletion
// via safe_delete_deferred() instead. The canvas widget (a child of root_)
// survives until the async delete tick, so destroy() must sever the canvas's
// image-source reference BEFORE freeing the canvas draw buffer — otherwise the
// still-live canvas would point at freed memory and a redraw in that window is
// a use-after-free. These tests exercise the full create + destroy roundtrip
// with seeded object geometry so the canvas path runs, then pump LVGL so the
// async delete completes. They crash/assert if the ordering regresses.

#include "printer_excluded_objects_state.h"

namespace {
// Seed a handful of objects with bounding boxes so create() allocates the
// canvas draw buffer and draws first-layer outlines into it.
void seed_objects(helix::PrinterExcludedObjectsState& st) {
    using ObjectInfo = helix::PrinterExcludedObjectsState::ObjectInfo;
    std::vector<ObjectInfo> objs;
    for (int i = 0; i < 3; ++i) {
        ObjectInfo o;
        o.name = "OBJ_" + std::to_string(i);
        float base = 20.0f + static_cast<float>(i) * 40.0f;
        o.bbox_min = {base, base};
        o.bbox_max = {base + 30.0f, base + 30.0f};
        o.center = {base + 15.0f, base + 15.0f};
        o.has_bbox = true;
        o.has_center = true;
        // Triangle polygon so draw_first_layer_outlines renders to the canvas.
        o.polygon = {{base, base}, {base + 30.0f, base}, {base + 15.0f, base + 30.0f}};
        objs.push_back(std::move(o));
    }
    st.set_defined_objects_with_geometry(objs);
}
} // namespace

TEST_CASE_METHOD(XMLTestFixture, "ExcludeObjectMapView create/destroy roundtrip is crash-free",
                 "[exclude_map][lifecycle]") {
    REQUIRE(register_component("components/exclude_object_map"));
    seed_objects(*state().get_excluded_objects_state());

    auto view = std::make_unique<ExcludeObjectMapView>();
    view->create(test_screen(), state().get_excluded_objects_state(), 235.0f, 235.0f,
                 /*exclude_manager=*/nullptr, /*parsed_file=*/nullptr);
    REQUIRE(view->is_active());

    // Let layout settle so the canvas buffer is allocated and drawn.
    process_lvgl(50);

    view->destroy();
    // After destroy(), root_ is deferred for async deletion; the view reports
    // inactive immediately so callers can't re-enter the live tree.
    REQUIRE_FALSE(view->is_active());

    // Pump LVGL so the lv_obj_delete_async tick actually deletes the widget
    // subtree (including the now-srcless canvas). If the canvas still pointed
    // at the freed draw buffer, a redraw during this window would crash.
    process_lvgl(50);

    // Idempotent: a second destroy() on an already-destroyed view is a no-op.
    view->destroy();
    REQUIRE_FALSE(view->is_active());

    // Destructor runs here — it must not double-free or touch freed widgets.
    view.reset();
    process_lvgl(20);
}

TEST_CASE_METHOD(XMLTestFixture,
                 "ExcludeObjectMapView destructor cleans up without explicit destroy",
                 "[exclude_map][lifecycle]") {
    REQUIRE(register_component("components/exclude_object_map"));
    seed_objects(*state().get_excluded_objects_state());

    const uint32_t baseline_children = lv_obj_get_child_count(test_screen());

    {
        ExcludeObjectMapView view;
        view.create(test_screen(), state().get_excluded_objects_state(), 200.0f, 200.0f, nullptr,
                    nullptr);
        REQUIRE(view.is_active());
        REQUIRE(lv_obj_get_child_count(test_screen()) > baseline_children);
        process_lvgl(30);
        // No explicit destroy(): the destructor must invoke destroy() and tear
        // down the canvas buffer + widget tree safely.
    }
    process_lvgl(50);

    // The heap canvas_buf_ leaks invisibly, but the widget subtree does not: a
    // destructor that skips destroy() leaves root_ parented to the screen. Child
    // count back at baseline is the observable proof that destroy() ran and its
    // deferred deletion completed - and destroy() is the only path that frees
    // the draw buffer too.
    REQUIRE(lv_obj_get_child_count(test_screen()) == baseline_children);
}
