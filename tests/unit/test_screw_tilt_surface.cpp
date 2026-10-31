// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screw_tilt_surface.h"

#include "../catch_amalgamated.hpp"

using helix::fit_screw_tilt_surface;
using helix::ScrewTiltSurface;

namespace {

ScrewTiltResult screw(float x, float y, float z) {
    ScrewTiltResult r;
    r.x_pos = x;
    r.y_pos = y;
    r.z_height = z;
    return r;
}

} // namespace

TEST_CASE("Four corner screws give a surface through every probed height",
          "[calibration][screws_tilt]") {
    // A twisted bed: no plane passes through all four.
    const std::vector<ScrewTiltResult> screws = {
        screw(30, 30, -0.02265f),   // front left
        screw(240, 30, 0.00841f),   // front right
        screw(240, 240, -0.02877f), // rear right
        screw(30, 240, -0.06507f),  // rear left
    };
    const ScrewTiltSurface s = fit_screw_tilt_surface(screws, 0.0);
    REQUIRE(s.valid);
    CHECK(s.z_at(0, 0) == Catch::Approx(-0.02265).margin(1e-6));
    CHECK(s.z_at(1, 0) == Catch::Approx(0.00841).margin(1e-6));
    CHECK(s.z_at(1, 1) == Catch::Approx(-0.02877).margin(1e-6));
    CHECK(s.z_at(0, 1) == Catch::Approx(-0.06507).margin(1e-6));
    // The middle is the corners' average on a bilinear patch.
    CHECK(s.z_at(0.5, 0.5) ==
          Catch::Approx((-0.02265 + 0.00841 - 0.02877 - 0.06507) / 4).margin(1e-6));
}

TEST_CASE("Three screws give the plane through them", "[calibration][screws_tilt]") {
    const std::vector<ScrewTiltResult> screws = {
        screw(0, 0, 0.0f),
        screw(200, 0, 0.2f),
        screw(100, 200, 0.4f),
    };
    const ScrewTiltSurface s = fit_screw_tilt_surface(screws, 0.0);
    REQUIRE(s.valid);
    CHECK(s.z_at(0, 0) == Catch::Approx(0.0).margin(1e-6));
    CHECK(s.z_at(1, 0) == Catch::Approx(0.2).margin(1e-6));
    CHECK(s.z_at(0.5, 1) == Catch::Approx(0.4).margin(1e-6));
    CHECK(s.coef[3] == 0.0);
}

TEST_CASE("No surface without enough spread-out screws", "[calibration][screws_tilt]") {
    SECTION("two screws") {
        CHECK_FALSE(fit_screw_tilt_surface({screw(0, 0, 0), screw(200, 200, 1)}, 0.0).valid);
    }
    SECTION("screws in a line") {
        CHECK_FALSE(
            fit_screw_tilt_surface({screw(0, 0, 0), screw(100, 0, 1), screw(200, 0, 2)}, 0.0)
                .valid);
    }
    SECTION("four screws on two columns of one row each is still a line") {
        CHECK_FALSE(fit_screw_tilt_surface(
                        {screw(0, 0, 0), screw(0, 0, 1), screw(200, 0, 0), screw(200, 0, 1)}, 0.0)
                        .valid);
    }
}

TEST_CASE("The colour scale spans the probed heights, never less than the floor",
          "[calibration][screws_tilt]") {
    const std::vector<ScrewTiltResult> screws = {screw(0, 0, 1.0f), screw(200, 0, 1.2f),
                                                 screw(200, 200, 1.1f), screw(0, 200, 1.0f)};

    SECTION("a spread wider than the floor fills the scale") {
        const ScrewTiltSurface s = fit_screw_tilt_surface(screws, 0.05);
        CHECK(s.spread_mm == Catch::Approx(0.2));
        CHECK(s.color_min == Catch::Approx(1.0));
        CHECK(s.color_max == Catch::Approx(1.2));
    }

    SECTION("a spread inside the floor stays near the middle of the scale") {
        const ScrewTiltSurface s = fit_screw_tilt_surface(screws, 0.5);
        CHECK(s.color_min == Catch::Approx(0.6));
        CHECK(s.color_max == Catch::Approx(1.6));
    }
}
