// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../include/bed_dimensions.h"
#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

TEST_CASE("bed_dimensions falls back to 235x235 with no api and no state", "[bed_dimensions]") {
    auto d = helix::bed_dimensions(nullptr, nullptr);
    REQUIRE(d.w_mm == Catch::Approx(235.0f));
    REQUIRE(d.h_mm == Catch::Approx(235.0f));
    REQUIRE(d.origin_x == Catch::Approx(0.0f));
    REQUIRE(d.origin_y == Catch::Approx(0.0f));
}

TEST_CASE("bed_dimensions rejects a degenerate build volume", "[bed_dimensions]") {
    // A build volume that has not been populated yet reports x_max == x_min,
    // giving a zero-width bed. That must fall through to the default rather
    // than produce a zero scale in BedCoordMapper.
    helix::BedDimensions d = helix::bed_dimensions_from_volume(0.0f, 0.0f, 0.0f, 0.0f);
    REQUIRE(d.w_mm == Catch::Approx(235.0f));
    REQUIRE(d.h_mm == Catch::Approx(235.0f));
}

TEST_CASE("bed_dimensions uses a populated build volume", "[bed_dimensions]") {
    helix::BedDimensions d = helix::bed_dimensions_from_volume(0.0f, 350.0f, 0.0f, 350.0f);
    REQUIRE(d.w_mm == Catch::Approx(350.0f));
    REQUIRE(d.h_mm == Catch::Approx(350.0f));
    REQUIRE(d.origin_x == Catch::Approx(0.0f));
}

TEST_CASE("bed_dimensions carries a negative origin through for delta beds", "[bed_dimensions]") {
    helix::BedDimensions d = helix::bed_dimensions_from_volume(-100.0f, 100.0f, -100.0f, 100.0f);
    REQUIRE(d.w_mm == Catch::Approx(200.0f));
    REQUIRE(d.h_mm == Catch::Approx(200.0f));
    REQUIRE(d.origin_x == Catch::Approx(-100.0f));
    REQUIRE(d.origin_y == Catch::Approx(-100.0f));
}

// Regression: a bed that genuinely measures 235x235 (Ender 3, Voron 0, ...) but
// reports a non-zero origin must NOT be misread as "no API data" just because its
// derived w_mm/h_mm happen to equal the default sentinel. Exercises tier 1 through
// a real IMoonrakerAPI so the sentinel-equality bug in bed_dimensions() (not
// bed_dimensions_from_volume, which is unaffected) actually gets hit.
TEST_CASE("bed_dimensions preserves origin for a coincidentally-235mm bed", "[bed_dimensions]") {
    MoonrakerClientMock client(MoonrakerClientMock::PrinterType::VORON_24);
    PrinterState state;
    state.init_subjects(false);
    MoonrakerAPIMock api(client, state);

    BuildVolume vol;
    vol.x_min = -5.0f;
    vol.x_max = 230.0f;
    vol.y_min = -5.0f;
    vol.y_max = 230.0f;
    api.hardware().set_build_volume(vol);

    helix::BedDimensions d = helix::bed_dimensions(&api, nullptr);
    REQUIRE(d.w_mm == Catch::Approx(235.0f));
    REQUIRE(d.h_mm == Catch::Approx(235.0f));
    REQUIRE(d.origin_x == Catch::Approx(-5.0f));
    REQUIRE(d.origin_y == Catch::Approx(-5.0f));
}
