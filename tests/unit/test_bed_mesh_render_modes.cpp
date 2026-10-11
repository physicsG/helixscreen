// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_bed_mesh_render_modes.cpp
 * @brief Both view modes render through bed_mesh_renderer_render_to_buffer, and the
 *        2D heatmap's drawing and touch hit-testing share one cell layout.
 */

#include "bed_mesh_buffer.h"
#include "bed_mesh_internal.h"
#include "bed_mesh_overlays.h"
#include "bed_mesh_renderer.h"

#include <cstring>

#include "../catch_amalgamated.hpp"

using Catch::Approx;
using helix::BedMeshRenderMode;
using helix::mesh::PixelBuffer;

#if HELIX_HAS_BED_MESH_3D
namespace {

constexpr int W = 200;
constexpr int H = 160;
constexpr uint8_t BG = 7; // distinct from every gradient color

struct Renderer {
    bed_mesh_renderer_t* r = bed_mesh_renderer_create();
    float rows[3][3] = {{0.0f, 0.1f, 0.2f}, {0.1f, 0.2f, 0.3f}, {0.2f, 0.3f, 0.4f}};
    Renderer() {
        const float* p[3] = {rows[0], rows[1], rows[2]};
        bed_mesh_renderer_set_mesh_data(r, p, 3, 3);
    }
    ~Renderer() {
        bed_mesh_renderer_destroy(r);
    }
};

bool is_bg(const PixelBuffer& buf, int x, int y) {
    const uint8_t* px = buf.pixel_at(x, y);
    return px[0] == BG && px[1] == BG && px[2] == BG;
}

PixelBuffer render(bed_mesh_renderer_t* r) {
    PixelBuffer buf(W, H);
    bed_mesh_render_colors_t colors{BG, BG, BG, 90, 90, 90};
    REQUIRE(bed_mesh_renderer_render_to_buffer(r, buf, colors));
    return buf;
}

} // namespace

TEST_CASE("2D heatmap renders every cell into the buffer", "[bed_mesh]") {
    Renderer m;
    bed_mesh_renderer_set_render_mode(m.r, BedMeshRenderMode::Force2D);
    PixelBuffer buf = render(m.r);

    REQUIRE(buf.info.heatmap);
    const auto l = helix::mesh::compute_heatmap_layout(buf.info.rows, buf.info.cols, W, H);
    REQUIRE(l.valid);
    REQUIRE(l.cells_x == 2);
    REQUIRE(l.cells_y == 2);
    for (int row = 0; row < l.cells_y; row++) {
        for (int col = 0; col < l.cells_x; col++) {
            int cx = l.grid_x + col * l.cell_w + l.cell_w / 2;
            int cy = l.grid_y + row * l.cell_h + l.cell_h / 4;
            INFO("cell " << row << "," << col);
            REQUIRE_FALSE(is_bg(buf, cx, cy));
        }
    }
    // Outside the grid stays background: the heatmap does not draw the 3D walls
    REQUIRE(is_bg(buf, 1, 1));
    REQUIRE(is_bg(buf, W - 2, H - 2));

    // Low corner (0.0) and high corner (0.4) land in different colors
    const uint8_t* low = buf.pixel_at(l.grid_x + 2, l.grid_y + l.cell_h / 2);
    const uint8_t* high =
        buf.pixel_at(l.grid_x + 2 * l.cell_w - 2, l.grid_y + 2 * l.cell_h - l.cell_h / 2);
    REQUIRE((low[0] != high[0] || low[1] != high[1] || low[2] != high[2]));
}

TEST_CASE("3D and 2D modes produce different frames", "[bed_mesh]") {
    Renderer m;
    PixelBuffer frame_3d = render(m.r);
    bed_mesh_renderer_set_render_mode(m.r, BedMeshRenderMode::Force2D);
    PixelBuffer frame_2d = render(m.r);
    bool same = std::memcmp(frame_3d.data(), frame_2d.data(), frame_3d.stride() * H) == 0;
    REQUIRE_FALSE(same);
}

TEST_CASE("2D touch hits the cell the heatmap drew there", "[bed_mesh]") {
    Renderer m;
    const auto l = helix::mesh::compute_heatmap_layout(3, 3, W, H);

    REQUIRE(bed_mesh_renderer_handle_touch(m.r, l.grid_x + l.cell_w + 1, l.grid_y + 1, l));
    REQUIRE(m.r->touched_row == 0);
    REQUIRE(m.r->touched_col == 1);
    REQUIRE(m.r->touched_z == Approx(0.1f));

    REQUIRE_FALSE(bed_mesh_renderer_handle_touch(m.r, W - 1, H - 1, l));
    REQUIRE_FALSE(m.r->touch_valid);
}

TEST_CASE("A frame keeps the mode and mesh it was rendered from", "[bed_mesh]") {
    Renderer m;
    bed_mesh_renderer_set_render_mode(m.r, BedMeshRenderMode::Force2D);
    PixelBuffer shown = render(m.r);

    // Mode switches to 3D, but no new frame has landed: the frame on screen is still
    // the heatmap, so its overlay and touch layout must come from it, not the mode.
    bed_mesh_renderer_set_render_mode(m.r, BedMeshRenderMode::Force3D);
    REQUIRE(shown.info.heatmap);
    REQUIRE(shown.info.rows == 3);
    REQUIRE(shown.info.cols == 3);
    const auto l = helix::mesh::compute_heatmap_layout(shown.info.rows, shown.info.cols,
                                                       shown.width(), shown.height());
    REQUIRE(bed_mesh_renderer_handle_touch(m.r, l.grid_x + 1, l.grid_y + l.cell_h + 1, l));
    REQUIRE(m.r->touched_row == 1);

    PixelBuffer next = render(m.r);
    REQUIRE_FALSE(next.info.heatmap);
}

TEST_CASE("Touch on a stale larger heatmap never indexes past a smaller mesh", "[bed_mesh]") {
    Renderer m;
    const auto stale = helix::mesh::compute_heatmap_layout(5, 5, W, H);
    // Cell (3,3) exists in the 5x5 frame on screen but not in the 3x3 mesh now loaded
    REQUIRE_FALSE(bed_mesh_renderer_handle_touch(m.r, stale.grid_x + 3 * stale.cell_w + 1,
                                                 stale.grid_y + 3 * stale.cell_h + 1, stale));
    REQUIRE_FALSE(m.r->touch_valid);
}

TEST_CASE("Heatmap layout needs at least a 2x2 mesh", "[bed_mesh]") {
    REQUIRE_FALSE(helix::mesh::compute_heatmap_layout(1, 3, W, H).valid);
    REQUIRE_FALSE(helix::mesh::compute_heatmap_layout(3, 1, W, H).valid);
    REQUIRE(helix::mesh::compute_heatmap_layout(2, 2, W, H).valid);
}

TEST_CASE("Bed extent follows the bed bounds once set, the mesh grid before", "[bed_mesh]") {
    Renderer m;

    auto before = helix::mesh::compute_bed_extent(m.r);
    REQUIRE(before.x_min_mm == 0.0);
    REQUIRE(before.x_max_mm == Approx(2 * BED_MESH_SCALE));
    REQUIRE(before.coord_scale == 1.0);
    REQUIRE(before.half_width == Approx(BED_MESH_SCALE));
    REQUIRE(before.walls.ceiling_z > before.walls.floor_z);

    bed_mesh_renderer_set_bounds(m.r, 0, 300, 0, 200, 20, 280, 20, 180);
    auto after = helix::mesh::compute_bed_extent(m.r);
    REQUIRE(after.x_max_mm == 300.0);
    REQUIRE(after.y_max_mm == 200.0);
    REQUIRE(after.center_x == 150.0);
    REQUIRE(after.coord_scale == Approx(m.r->coord_scale));
    REQUIRE(after.half_width == Approx(150.0 * m.r->coord_scale));
    REQUIRE(after.half_height == Approx(100.0 * m.r->coord_scale));
}
#endif
