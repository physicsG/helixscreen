// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_bed_mesh_static_mode.cpp
 * @brief <bed_mesh> static mode: a frame rendered on the main thread when something it shows
 * changes, drawn again for free until the next change, with no render thread.
 *
 * Run with: ./build/bin/helix-tests "[bed_mesh][static_mode]"
 */

#include "ui_bed_mesh.h"

#include "../lvgl_ui_test_fixture.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "theme_manager.h"

#include <vector>

#include "../catch_amalgamated.hpp"

#if HELIX_HAS_BED_MESH_3D

using namespace helix;

namespace {

struct StaticModeFixture : public LVGLUITestFixture {
    lv_obj_t* make_canvas(int w, int h) {
        canvas = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "bed_mesh", nullptr));
        REQUIRE(canvas != nullptr);
        lv_obj_set_size(canvas, w, h);
        lv_obj_update_layout(canvas);
        feed();
        return canvas;
    }
    void feed(float tilt = 0.1f) {
        std::vector<std::vector<float>> rows(7, std::vector<float>(7));
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < 7; ++c)
                rows[static_cast<size_t>(r)][static_cast<size_t>(c)] =
                    tilt * static_cast<float>(r + c) / 12.0f;
        std::vector<const float*> ptrs;
        for (const auto& row : rows)
            ptrs.push_back(row.data());
        REQUIRE(ui_bed_mesh_set_data(canvas, ptrs.data(), 7, 7));
        ui_bed_mesh_set_bounds(canvas, 0, 220, 0, 220, 20, 200, 20, 200);
    }
    void draw() {
        lv_obj_invalidate(canvas);
        lv_refr_now(nullptr);
    }
    lv_obj_t* canvas = nullptr;
};

} // namespace

TEST_CASE_METHOD(StaticModeFixture, "static mode draws nothing until it is turned on",
                 "[bed_mesh][static_mode]") {
    make_canvas(200, 200);
    draw();
    CHECK(ui_bed_mesh_static_render_count(canvas) == 0);
    CHECK_FALSE(ui_bed_mesh_is_async_mode(canvas));
}

TEST_CASE_METHOD(StaticModeFixture,
                 "static mode renders once and draws the same frame until a change",
                 "[bed_mesh][static_mode]") {
    make_canvas(200, 200);
    ui_bed_mesh_set_render_mode(canvas, BedMeshRenderMode::Force2D);
    ui_bed_mesh_set_static_mode(canvas, true);

    draw();
    CHECK(ui_bed_mesh_static_render_count(canvas) == 1);
    CHECK_FALSE(ui_bed_mesh_is_async_mode(canvas));
    draw();
    draw();
    CHECK(ui_bed_mesh_static_render_count(canvas) == 1);

    feed(0.3f); // a new mesh
    draw();
    CHECK(ui_bed_mesh_static_render_count(canvas) == 2);

    ui_bed_mesh_set_render_mode(canvas, BedMeshRenderMode::Force3D);
    draw();
    CHECK(ui_bed_mesh_static_render_count(canvas) == 3);

    lv_obj_set_size(canvas, 240, 160); // a new size
    lv_obj_update_layout(canvas);
    draw();
    CHECK(ui_bed_mesh_static_render_count(canvas) == 4);
}

TEST_CASE_METHOD(StaticModeFixture,
                 "static mode leaves the widget without a render thread or interaction state",
                 "[bed_mesh][static_mode]") {
    make_canvas(200, 200);
    ui_bed_mesh_set_static_mode(canvas, true);
    draw();
    CHECK_FALSE(ui_bed_mesh_is_async_mode(canvas));

    ui_bed_mesh_set_static_mode(canvas, false);
    const int rendered = ui_bed_mesh_static_render_count(canvas);
    draw();
    CHECK(ui_bed_mesh_static_render_count(canvas) == rendered);
}

TEST_CASE_METHOD(StaticModeFixture, "static mode renders nothing without mesh data",
                 "[bed_mesh][static_mode]") {
    canvas = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "bed_mesh", nullptr));
    REQUIRE(canvas != nullptr);
    lv_obj_set_size(canvas, 200, 200);
    ui_bed_mesh_set_static_mode(canvas, true);
    draw();
    CHECK_FALSE(ui_bed_mesh_has_data(canvas));
    CHECK(ui_bed_mesh_static_render_count(canvas) == 0);
}

TEST_CASE_METHOD(StaticModeFixture, "static mode renders again when the theme changes",
                 "[bed_mesh][static_mode]") {
    make_canvas(200, 200);
    ui_bed_mesh_set_render_mode(canvas, BedMeshRenderMode::Force2D);
    ui_bed_mesh_set_static_mode(canvas, true);
    draw();
    draw();
    REQUIRE(ui_bed_mesh_static_render_count(canvas) == 1);

    theme_manager_notify_change();
    draw();
    CHECK(ui_bed_mesh_static_render_count(canvas) == 2);
    draw();
    CHECK(ui_bed_mesh_static_render_count(canvas) == 2);
}

TEST_CASE_METHOD(StaticModeFixture, "a static 3D render over budget keeps the widget on 2D",
                 "[bed_mesh][static_mode]") {
    make_canvas(200, 200);
    ui_bed_mesh_set_static_mode(canvas, true);
    ui_bed_mesh_set_render_mode(canvas, BedMeshRenderMode::Force3D);
    REQUIRE_FALSE(ui_bed_mesh_static_3d_banned(canvas));

    ui_bed_mesh_set_static_render_budget_ms_for_test(0.0001f);
    draw();
    ui_bed_mesh_set_static_render_budget_ms_for_test(0.0f);
    REQUIRE(ui_bed_mesh_static_3d_banned(canvas));
    CHECK(ui_bed_mesh_get_render_mode(canvas) == BedMeshRenderMode::Force2D);

    ui_bed_mesh_set_render_mode(canvas, BedMeshRenderMode::Force3D);
    CHECK(ui_bed_mesh_get_render_mode(canvas) == BedMeshRenderMode::Force2D);
}

#endif // HELIX_HAS_BED_MESH_3D
