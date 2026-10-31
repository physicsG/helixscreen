// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "bed_grid.h"

#include <cmath>
#include <vector>

#include "../catch_amalgamated.hpp"

using Catch::Approx;
using helix::bed_grid_lines;
using helix::BedGridSegment;
using helix::BedPoint;
using helix::clip_to_round_bed;
using helix::RoundBed;
using helix::segment_in_round_bed;

namespace {

std::vector<double> vertical_xs(const std::vector<BedGridSegment>& lines) {
    std::vector<double> xs;
    for (const auto& s : lines) {
        if (s.x1 == s.x2) {
            xs.push_back(s.x1);
        }
    }
    return xs;
}

std::vector<double> horizontal_ys(const std::vector<BedGridSegment>& lines) {
    std::vector<double> ys;
    for (const auto& s : lines) {
        if (s.y1 == s.y2) {
            ys.push_back(s.y1);
        }
    }
    return ys;
}

} // namespace

TEST_CASE("rectangular bed grid: interior multiples, edge to edge", "[bed_grid]") {
    const auto lines = bed_grid_lines(0, 235, 0, 220, 50, false);

    // 0 is the perimeter; 250 is past the bed.
    CHECK(vertical_xs(lines) == std::vector<double>{50, 100, 150, 200});
    CHECK(horizontal_ys(lines) == std::vector<double>{50, 100, 150, 200});
    REQUIRE(lines.size() == 8);
    for (const auto& s : lines) {
        if (s.x1 == s.x2) {
            CHECK(s.y1 == 0.0);
            CHECK(s.y2 == 220.0);
        } else {
            CHECK(s.x1 == 0.0);
            CHECK(s.x2 == 235.0);
        }
    }
}

TEST_CASE("rectangular bed grid leaves out lines on an aligned perimeter", "[bed_grid]") {
    const auto lines = bed_grid_lines(0, 300, 0, 300, 50, false);
    CHECK(vertical_xs(lines) == std::vector<double>{50, 100, 150, 200, 250});
}

TEST_CASE("rectangular bed grid on negative coordinates", "[bed_grid]") {
    const auto lines = bed_grid_lines(-120, 120, -60, 60, 50, false);
    CHECK(vertical_xs(lines) == std::vector<double>{-100, -50, 0, 50, 100});
    CHECK(horizontal_ys(lines) == std::vector<double>{-50, 0, 50});
}

TEST_CASE("round bed grid: chords of the inscribed circle", "[bed_grid]") {
    // A centred delta: lines at -100..100, the +-125 perimeter excluded.
    const auto lines = bed_grid_lines(-125, 125, -125, 125, 50, true);
    CHECK(vertical_xs(lines) == std::vector<double>{-100, -50, 0, 50, 100});
    CHECK(horizontal_ys(lines) == std::vector<double>{-100, -50, 0, 50, 100});

    for (const auto& s : lines) {
        INFO("segment " << s.x1 << "," << s.y1 << " -> " << s.x2 << "," << s.y2);
        // Both ends sit on the circle.
        CHECK(std::hypot(s.x1, s.y1) == Approx(125.0));
        CHECK(std::hypot(s.x2, s.y2) == Approx(125.0));
    }
    // The diameter spans the whole bed; the chord at 100 is 2*sqrt(125^2-100^2).
    for (const auto& s : lines) {
        if (s.x1 == 0.0 && s.x2 == 0.0) {
            CHECK(s.y2 - s.y1 == Approx(250.0));
        }
        if (s.x1 == 100.0 && s.x2 == 100.0) {
            CHECK(s.y2 - s.y1 == Approx(150.0));
        }
    }
}

TEST_CASE("round bed grid on an offset box uses the box centre", "[bed_grid]") {
    // A 0-based 250 box: centre (125,125), radius 125.
    const auto lines = bed_grid_lines(0, 250, 0, 250, 50, true);
    CHECK(vertical_xs(lines) == std::vector<double>{50, 100, 150, 200});
    for (const auto& s : lines) {
        CHECK(std::hypot(s.x1 - 125, s.y1 - 125) == Approx(125.0));
        CHECK(std::hypot(s.x2 - 125, s.y2 - 125) == Approx(125.0));
    }
}

TEST_CASE("round bed grid inscribes the circle in a non-square box", "[bed_grid]") {
    // Radius is the smaller half-size (100): x = +-150 lies outside the circle.
    const auto lines = bed_grid_lines(-200, 200, -100, 100, 50, true);
    CHECK(vertical_xs(lines) == std::vector<double>{-50, 0, 50});
    CHECK(horizontal_ys(lines) == std::vector<double>{-50, 0, 50});
    for (const auto& s : lines) {
        CHECK(std::hypot(s.x1, s.y1) == Approx(100.0));
    }
}

TEST_CASE("bed grid is empty for a degenerate box or spacing", "[bed_grid]") {
    CHECK(bed_grid_lines(0, 0, 0, 200, 50, false).empty());
    CHECK(bed_grid_lines(100, 0, 0, 200, 50, true).empty());
    CHECK(bed_grid_lines(0, 200, 0, 200, 0, false).empty());
    CHECK(bed_grid_lines(0, 200, 0, 200, -50, true).empty());
    // A bed smaller than one spacing has no interior line.
    CHECK(bed_grid_lines(10, 40, 10, 40, 50, false).empty());
}

TEST_CASE("inscribed round bed is centred, radius the smaller half-size", "[bed_grid]") {
    const auto bed = helix::inscribed_round_bed(0, 300, -50, 150);
    CHECK(bed.cx == 150.0);
    CHECK(bed.cy == 50.0);
    CHECK(bed.r == 100.0);
}

TEST_CASE("clip to round bed keeps a polygon wholly inside unchanged", "[bed_grid]") {
    const RoundBed bed{0, 0, 100};
    const std::vector<BedPoint> cell{{-10, -10}, {10, -10}, {10, 10}, {-10, 10}};
    const auto out = clip_to_round_bed(cell, bed);
    REQUIRE(out.size() == cell.size());
    for (size_t i = 0; i < out.size(); i++) {
        CHECK(out[i].x == cell[i].x);
        CHECK(out[i].y == cell[i].y);
    }
}

TEST_CASE("clip to round bed drops a polygon wholly outside", "[bed_grid]") {
    const RoundBed bed{0, 0, 100};
    // The corner cell of the bounding box: entirely beyond the circle.
    CHECK(clip_to_round_bed({{80, 80}, {100, 80}, {100, 100}, {80, 100}}, bed).empty());
}

TEST_CASE("clip to round bed trims a straddling cell to the circle", "[bed_grid]") {
    const RoundBed bed{0, 0, 100};
    const std::vector<BedPoint> cell{{50, 50}, {100, 50}, {100, 100}, {50, 100}};
    const auto out = clip_to_round_bed(cell, bed);
    REQUIRE(out.size() >= 3);
    bool touches_circle = false;
    for (const auto& p : out) {
        const double d = std::hypot(p.x, p.y);
        CHECK(d <= 100.0 + 1e-9);
        if (d > 99.0) {
            touches_circle = true;
        }
        // Still inside the cell.
        CHECK(p.x >= 50.0 - 1e-9);
        CHECK(p.y >= 50.0 - 1e-9);
    }
    CHECK(touches_circle);
    // The inner corner survives.
    bool has_inner_corner = false;
    for (const auto& p : out) {
        has_inner_corner |= p.x == Approx(50.0) && p.y == Approx(50.0);
    }
    CHECK(has_inner_corner);
}

TEST_CASE("clip to round bed honours an off-origin centre", "[bed_grid]") {
    const RoundBed bed{125, 125, 125};
    // The cell around the centre is inside; the box corner cell is outside.
    CHECK(clip_to_round_bed({{100, 100}, {150, 100}, {150, 150}, {100, 150}}, bed).size() == 4);
    CHECK(clip_to_round_bed({{0, 0}, {20, 0}, {20, 20}, {0, 20}}, bed).empty());
}

TEST_CASE("segment in round bed", "[bed_grid]") {
    const RoundBed bed{0, 0, 100};

    SECTION("wholly inside is the full range") {
        auto t = segment_in_round_bed({-50, 0}, {50, 0}, bed);
        REQUIRE(t);
        CHECK(t->first == 0.0);
        CHECK(t->second == 1.0);
    }
    SECTION("crossing the circle is cut where it meets it") {
        // From the centre out to x=200: inside up to x=100, half way.
        auto t = segment_in_round_bed({0, 0}, {200, 0}, bed);
        REQUIRE(t);
        CHECK(t->first == 0.0);
        CHECK(t->second == Approx(0.5));
    }
    SECTION("a chord through both sides") {
        auto t = segment_in_round_bed({-200, 60}, {200, 60}, bed);
        REQUIRE(t);
        // x = +-80 on y = 60.
        CHECK(t->first == Approx(120.0 / 400.0));
        CHECK(t->second == Approx(280.0 / 400.0));
    }
    SECTION("wholly outside is nothing") {
        CHECK_FALSE(segment_in_round_bed({90, 90}, {100, 100}, bed));
        CHECK_FALSE(segment_in_round_bed({-200, 150}, {200, 150}, bed));
    }
}

TEST_CASE("bed outline: four edges, or a polygon on the inscribed circle", "[bed_grid]") {
    const auto rect = helix::bed_outline_segments(0, 300, -20, 200, false);
    REQUIRE(rect.size() == 4);
    for (const auto& s : rect) {
        // Every edge runs along the box.
        const bool on_x_edge = (s.x1 == 0 || s.x1 == 300) && s.x1 == s.x2;
        const bool on_y_edge = (s.y1 == -20 || s.y1 == 200) && s.y1 == s.y2;
        CHECK((on_x_edge || on_y_edge));
    }

    const auto round = helix::bed_outline_segments(-125, 125, -125, 125, true);
    REQUIRE(round.size() == static_cast<size_t>(helix::kRoundBedSegments));
    for (size_t i = 0; i < round.size(); i++) {
        CHECK(std::hypot(round[i].x1, round[i].y1) == Approx(125.0));
        // Closed: each segment starts where the previous ended.
        const auto& prev = round[(i + round.size() - 1) % round.size()];
        CHECK(round[i].x1 == Approx(prev.x2).margin(1e-9));
        CHECK(round[i].y1 == Approx(prev.y2).margin(1e-9));
    }

    CHECK(helix::bed_outline_segments(0, 0, 0, 100, true).empty());
}
