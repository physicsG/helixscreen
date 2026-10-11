// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../include/bed_coord_mapper.h"

#include "../catch_amalgamated.hpp"

using helix::BedCoordMapper;

TEST_CASE("BedCoordMapper preserves aspect ratio on a non-square viewport", "[bed_coord_mapper]") {
    // 200x100mm bed into a 400x400px viewport: the limiting axis is X
    // (400/200 = 2.0) vs Y (400/100 = 4.0), so scale must be 2.0, not 4.0.
    BedCoordMapper m(200.0f, 100.0f, 400, 400);
    REQUIRE(m.scale() == Catch::Approx(2.0f));
}

TEST_CASE("BedCoordMapper flips Y so bed origin is bottom-left", "[bed_coord_mapper]") {
    // Square bed, square viewport, scale 1.0, no centering slack on X.
    BedCoordMapper m(100.0f, 100.0f, 100, 100);

    auto [x0, y0] = m.mm_to_px(0.0f, 0.0f);
    auto [x1, y1] = m.mm_to_px(0.0f, 100.0f);

    // y=0mm is the BOTTOM of the plate, so it maps to the LARGER pixel y.
    REQUIRE(y0 > y1);
    REQUIRE(y0 == Catch::Approx(100.0f));
    REQUIRE(y1 == Catch::Approx(0.0f));
    REQUIRE(x0 == Catch::Approx(x1));
}

TEST_CASE("BedCoordMapper centers the plate in the slack axis", "[bed_coord_mapper]") {
    // 100x100mm bed into 200x100px: scale = 1.0 (Y-limited), leaving 100px
    // of horizontal slack that must be split evenly.
    BedCoordMapper m(100.0f, 100.0f, 200, 100);
    REQUIRE(m.scale() == Catch::Approx(1.0f));

    auto [x_left, y_left] = m.mm_to_px(0.0f, 0.0f);
    REQUIRE(x_left == Catch::Approx(50.0f));
}

TEST_CASE("BedCoordMapper honours a center origin for delta beds", "[bed_coord_mapper]") {
    // A 200mm delta bed spans -100..+100. Its center (0,0) must land in the
    // middle of the viewport, not at a corner.
    BedCoordMapper m(200.0f, 200.0f, 200, 200, -100.0f, -100.0f);

    auto [cx, cy] = m.mm_to_px(0.0f, 0.0f);
    REQUIRE(cx == Catch::Approx(100.0f));
    REQUIRE(cy == Catch::Approx(100.0f));
}

TEST_CASE("BedCoordMapper px_to_mm round-trips on a wide viewport (letterboxed X)",
          "[bed_coord_mapper]") {
    // 100x100mm bed into 400x200px: scale 2.0, plate centered with 100px of
    // slack on each side in X.
    BedCoordMapper m(100.0f, 100.0f, 400, 200);
    const float pts[][2] = {{0, 0}, {100, 100}, {50, 50}, {12.5, 87.5}, {0, 100}, {100, 0}};
    for (const auto& p : pts) {
        auto [px, py] = m.mm_to_px(p[0], p[1]);
        auto [x, y] = m.px_to_mm(px, py);
        INFO("mm(" << p[0] << "," << p[1] << ") -> px(" << px << "," << py << ") -> mm(" << x << ","
                   << y << ")");
        REQUIRE(x == Catch::Approx(p[0]).margin(1e-3f));
        REQUIRE(y == Catch::Approx(p[1]).margin(1e-3f));
    }
}

TEST_CASE("BedCoordMapper px_to_mm round-trips on a tall viewport (letterboxed Y)",
          "[bed_coord_mapper]") {
    // 100x100mm bed into 200x400px: scale 2.0, slack is vertical now.
    BedCoordMapper m(100.0f, 100.0f, 200, 400);
    const float pts[][2] = {{0, 0}, {100, 100}, {50, 50}, {33.3, 66.6}};
    for (const auto& p : pts) {
        auto [px, py] = m.mm_to_px(p[0], p[1]);
        auto [x, y] = m.px_to_mm(px, py);
        REQUIRE(x == Catch::Approx(p[0]).margin(1e-3f));
        REQUIRE(y == Catch::Approx(p[1]).margin(1e-3f));
    }
}

TEST_CASE("BedCoordMapper px_to_mm round-trips a centre-origin delta bed", "[bed_coord_mapper]") {
    BedCoordMapper m(200.0f, 200.0f, 200, 200, -100.0f, -100.0f);
    const float pts[][2] = {{-100, -100}, {100, 100}, {0, 0}, {-42.5, 17.5}};
    for (const auto& p : pts) {
        auto [px, py] = m.mm_to_px(p[0], p[1]);
        auto [x, y] = m.px_to_mm(px, py);
        REQUIRE(x == Catch::Approx(p[0]).margin(1e-3f));
        REQUIRE(y == Catch::Approx(p[1]).margin(1e-3f));
    }
}

TEST_CASE("BedCoordMapper px_to_mm maps a letterbox-margin point outside the bed",
          "[bed_coord_mapper]") {
    // Same wide-viewport geometry as the round-trip test: x slack is 100px per
    // side, so px x=50 sits in the left margin and must invert to a negative
    // mm x. The mapper is deliberately unclamped; callers clamp.
    BedCoordMapper m(100.0f, 100.0f, 400, 200);
    auto [x, y] = m.px_to_mm(50.0f, 100.0f);
    REQUIRE(x < 0.0f);
    REQUIRE(y >= 0.0f);
    REQUIRE(y <= 100.0f);
}
