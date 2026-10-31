// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "bed_plate_draw.h"
#include "motion_presets.h"

#include <cmath>

#include "../catch_amalgamated.hpp"

using Catch::Approx;
using helix::BedCoordMapper;
using helix::BedObjectOutline;
using helix::BedPlateLayersInput;
using helix::plan_bed_plate_layers;

namespace {

BedPlateLayersInput square_bed(float size_mm) {
    BedPlateLayersInput in;
    in.x_min = 0.0f;
    in.x_max = size_mm;
    in.y_min = 0.0f;
    in.y_max = size_mm;
    return in;
}

BedCoordMapper mapper_for(const BedPlateLayersInput& in, int viewport_px) {
    return BedCoordMapper(in.x_max - in.x_min, in.y_max - in.y_min, viewport_px, viewport_px,
                          in.x_min, in.y_min);
}

BedObjectOutline square_outline(float x1, float y1, float x2, float y2) {
    return BedObjectOutline{{{x1, y1}, {x2, y1}, {x2, y2}, {x1, y2}}};
}

} // namespace

TEST_CASE("plate grid: a 350mm bed in 357px draws six lines per axis", "[bed_plate_draw]") {
    const auto in = square_bed(350.0f);
    const auto layers = plan_bed_plate_layers(in, mapper_for(in, 357));
    REQUIRE(layers.grid.size() == 12);
    for (const auto& s : layers.grid) {
        for (float v : {s.x1, s.y1, s.x2, s.y2}) {
            CHECK(v >= 0.0f);
            CHECK(v <= 357.0f);
        }
    }
}

TEST_CASE("plate grid: left out when 50mm is under the minimum spacing", "[bed_plate_draw]") {
    const auto in = square_bed(350.0f);
    // 50mm at 60px / 350mm is about 8.6px.
    CHECK(plan_bed_plate_layers(in, mapper_for(in, 60)).grid.empty());
}

TEST_CASE("plate grid: a round bed's lines stay inside its circle", "[bed_plate_draw]") {
    auto in = square_bed(300.0f);
    in.circular = true;
    const int vp = 300;
    const auto layers = plan_bed_plate_layers(in, mapper_for(in, vp));
    REQUIRE_FALSE(layers.grid.empty());
    const float c = vp / 2.0f;
    for (const auto& s : layers.grid) {
        CHECK(std::hypot(s.x1 - c, s.y1 - c) <= c + 0.01f);
        CHECK(std::hypot(s.x2 - c, s.y2 - c) <= c + 0.01f);
    }
}

TEST_CASE("plate outlines map through the mapper", "[bed_plate_draw]") {
    auto in = square_bed(350.0f);
    in.outlines.push_back(square_outline(100, 100, 150, 150));
    const auto mapper = mapper_for(in, 357);
    const auto layers = plan_bed_plate_layers(in, mapper);
    REQUIRE(layers.outlines.size() == 1);
    REQUIRE(layers.outlines[0].size() == 4);
    const auto [x0, y0] = mapper.mm_to_px(100, 100);
    const auto [x2, y2] = mapper.mm_to_px(150, 150);
    CHECK(layers.outlines[0][0].x == Approx(x0));
    CHECK(layers.outlines[0][0].y == Approx(y0));
    CHECK(layers.outlines[0][2].x == Approx(x2));
    CHECK(layers.outlines[0][2].y == Approx(y2));
}

TEST_CASE("plate outlines: a point off the plate is clamped to its edge", "[bed_plate_draw]") {
    auto in = square_bed(350.0f);
    in.outlines.push_back(BedObjectOutline{{{-20, 100}, {50, 100}, {50, 150}}});
    const auto mapper = mapper_for(in, 357);
    const auto layers = plan_bed_plate_layers(in, mapper);
    REQUIRE(layers.outlines.size() == 1);
    const auto [edge_x, edge_y] = mapper.mm_to_px(0, 100);
    CHECK(layers.outlines[0][0].x == Approx(edge_x));
    CHECK(layers.outlines[0][0].y == Approx(edge_y));
}

TEST_CASE("plate outlines: fewer than three points are dropped", "[bed_plate_draw]") {
    auto in = square_bed(350.0f);
    in.outlines.push_back(BedObjectOutline{{{10, 10}, {20, 20}}});
    in.outlines.push_back(square_outline(100, 100, 150, 150));
    const auto layers = plan_bed_plate_layers(in, mapper_for(in, 357));
    REQUIRE(layers.outlines.size() == 1);
    CHECK(layers.outlines[0].size() == 4);
}
