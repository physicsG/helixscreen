// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bed_mesh_projection.h"
#include "bed_mesh_renderer.h"

#include <cmath>
#include <limits>

#include "../catch_amalgamated.hpp"

using Catch::Approx;

#if HELIX_HAS_BED_MESH_3D
namespace {
constexpr int W = 600;
constexpr int H = 400;

bed_mesh_view_state_t make_view() {
    bed_mesh_view_state_t v{};
    v.angle_x = 0.0;
    v.angle_z = 0.0;
    v.z_scale = 60.0;
    v.fov_scale = 150.0;
    v.camera_distance = 1000.0;
    v.cached_cos_x = 1.0;
    v.cached_sin_x = 0.0;
    v.cached_cos_z = 1.0;
    v.cached_sin_z = 0.0;
    v.trig_cache_valid = true;
    v.center_offset_x = 7;
    v.center_offset_y = -5;
    v.zoom = 1.0;
    v.pan_x = 0.0;
    v.pan_y = 0.0;
    return v;
}

bed_mesh_point_3d_t project(const bed_mesh_view_state_t& v, double x, double y, double z) {
    return bed_mesh_projection_project_3d_to_2d(x, y, z, W, H, &v);
}
} // namespace

TEST_CASE("bed mesh projection: default zoom and pan match the plain perspective formula",
          "[bed_mesh][zoom]") {
    const auto v = make_view();
    const double x = 120.0, y = -80.0, z = 30.0;
    // Rotation is identity: final_y = y*cos_x - z*sin_x = y, final_z = dist - (y*sin_x + z*cos_x)
    const double fz = v.camera_distance - z;
    const int ex = static_cast<int>(W / 2 + (x * v.fov_scale) / fz) + v.center_offset_x;
    const int ey = static_cast<int>(H * BED_MESH_Z_ORIGIN_VERTICAL_POS + (y * v.fov_scale) / fz) +
                   v.center_offset_y;
    const auto p = project(v, x, y, z);
    CHECK(p.screen_x == ex);
    CHECK(p.screen_y == ey);
}

TEST_CASE("bed mesh projection: pan shifts every point by the pan offset", "[bed_mesh][zoom]") {
    auto v = make_view();
    v.zoom = 2.0;
    const auto base = project(v, 50.0, 40.0, 10.0);
    v.pan_x = 30.0;
    v.pan_y = -12.0;
    const auto moved = project(v, 50.0, 40.0, 10.0);
    CHECK(moved.screen_x - base.screen_x == Approx(30).margin(1));
    CHECK(moved.screen_y - base.screen_y == Approx(-12).margin(1));
}

TEST_CASE("bed mesh zoom_at keeps the anchor fixed across successive zooms", "[bed_mesh][zoom]") {
    auto v = make_view();
    const auto a = project(v, 150.0, 90.0, 0.0);
    bed_mesh_projection_zoom_at(&v, 1.5, a.screen_x, a.screen_y, W, H);
    bed_mesh_projection_zoom_at(&v, 2.0, a.screen_x, a.screen_y, W, H);
    const auto after = project(v, 150.0, 90.0, 0.0);
    CHECK(v.zoom == Approx(3.0));
    CHECK(after.screen_x == Approx(a.screen_x).margin(1));
    CHECK(after.screen_y == Approx(a.screen_y).margin(1));
}

TEST_CASE("bed mesh zoom_at clamps at 8x and still holds the anchor", "[bed_mesh][zoom]") {
    auto v = make_view();
    const auto a = project(v, -100.0, 60.0, 0.0);
    bed_mesh_projection_zoom_at(&v, 100.0, a.screen_x, a.screen_y, W, H);
    CHECK(v.zoom == Approx(BED_MESH_ZOOM_MAX));
    const auto after = project(v, -100.0, 60.0, 0.0);
    CHECK(after.screen_x == Approx(a.screen_x).margin(1));
    CHECK(after.screen_y == Approx(a.screen_y).margin(1));
}

TEST_CASE("bed mesh zooming out to 1x snaps pan to zero", "[bed_mesh][zoom]") {
    auto v = make_view();
    bed_mesh_projection_zoom_at(&v, 2.0, 100.0, 100.0, W, H);
    bed_mesh_projection_pan(&v, 40.0, -25.0);
    REQUIRE(v.pan_x != 0.0);
    bed_mesh_projection_zoom_at(&v, 0.3, 100.0, 100.0, W, H);
    CHECK(v.zoom == Approx(BED_MESH_ZOOM_MIN));
    CHECK(v.pan_x == 0.0);
    CHECK(v.pan_y == 0.0);
}

TEST_CASE("bed mesh pan is a no-op at 1x", "[bed_mesh][zoom]") {
    auto v = make_view();
    bed_mesh_projection_pan(&v, 40.0, -25.0);
    CHECK(v.pan_x == 0.0);
    CHECK(v.pan_y == 0.0);
}

TEST_CASE("bed mesh zoom_at ignores non-positive and NaN factors", "[bed_mesh][zoom]") {
    auto v = make_view();
    bed_mesh_projection_zoom_at(&v, 2.0, 100.0, 100.0, W, H);
    const auto before = v;
    bed_mesh_projection_zoom_at(&v, 0.0, 100.0, 100.0, W, H);
    bed_mesh_projection_zoom_at(&v, -2.0, 100.0, 100.0, W, H);
    bed_mesh_projection_zoom_at(&v, std::numeric_limits<double>::quiet_NaN(), 100.0, 100.0, W, H);
    CHECK(v.zoom == before.zoom);
    CHECK(v.pan_x == before.pan_x);
    CHECK(v.pan_y == before.pan_y);
}

TEST_CASE("bed mesh renderer: set_bounds and set_render_mode reset zoom and pan",
          "[bed_mesh][zoom]") {
    bed_mesh_renderer_t* r = bed_mesh_renderer_create();
    REQUIRE(r != nullptr);

    bed_mesh_renderer_apply_two_finger(r, 0.0, 0.0, 3.0, 100.0, 100.0, W, H);
    bed_mesh_renderer_apply_two_finger(r, 20.0, 10.0, 1.0, 100.0, 100.0, W, H);
    REQUIRE(bed_mesh_renderer_get_view_state(r)->zoom == Approx(3.0));
    REQUIRE(bed_mesh_renderer_get_view_state(r)->pan_x != 0.0);

    bed_mesh_renderer_set_bounds(r, 0, 235, 0, 235, 10, 225, 10, 225);
    CHECK(bed_mesh_renderer_get_view_state(r)->zoom == 1.0);
    CHECK(bed_mesh_renderer_get_view_state(r)->pan_x == 0.0);
    CHECK(bed_mesh_renderer_get_view_state(r)->pan_y == 0.0);

    bed_mesh_renderer_apply_two_finger(r, 0.0, 0.0, 2.0, 100.0, 100.0, W, H);
    bed_mesh_renderer_set_render_mode(r, helix::BedMeshRenderMode::Force3D);
    CHECK(bed_mesh_renderer_get_view_state(r)->zoom == 1.0);

    bed_mesh_renderer_destroy(r);
}
#endif
