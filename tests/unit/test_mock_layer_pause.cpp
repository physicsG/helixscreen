// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_mock_layer_pause.cpp
 * @brief The mock printer's client.cfg layer-pause macros (#1703).
 *
 * The mock must parse each command the way the client.cfg macro bodies do, or
 * --test shows a schedule no real printer would hold.
 */

#include "../test_helpers/scoped_env.h"
#include "lvgl_test_fixture.h"
#include "moonraker_client_mock.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using nlohmann::json;

namespace {

class MockLayerPauseFixture : public LVGLTestFixture {
  public:
    MockLayerPauseFixture() : client(MoonrakerClientMock::PrinterType::VORON_24) {}

    json schedule() {
        return client.layer_pause_status()["gcode_macro SET_PRINT_STATS_INFO"];
    }

    MoonrakerClientMock client;
};

} // namespace

TEST_CASE_METHOD(MockLayerPauseFixture, "Mock layer pause: commands follow client.cfg",
                 "[layer_pause][mock]") {
    REQUIRE(MoonrakerClientMock::layer_pause_macros_loaded());
    CHECK(schedule()["pause_next_layer"]["enable"] == false);
    CHECK(schedule()["pause_at_layer"]["enable"] == false);

    SECTION("NEXT_LAYER defaults ENABLE to 1, and ENABLE=0 clears") {
        client.gcode_script("SET_PAUSE_NEXT_LAYER");
        CHECK(schedule()["pause_next_layer"]["enable"] == true);
        client.gcode_script("SET_PAUSE_NEXT_LAYER ENABLE=0");
        CHECK(schedule()["pause_next_layer"]["enable"] == false);
    }
    SECTION("AT_LAYER enables when LAYER is given, and ENABLE=0 keeps the layer") {
        client.gcode_script("SET_PAUSE_AT_LAYER ENABLE=1 LAYER=42");
        CHECK(schedule()["pause_at_layer"]["enable"] == true);
        CHECK(schedule()["pause_at_layer"]["layer"] == 42);
        client.gcode_script("SET_PAUSE_AT_LAYER ENABLE=0");
        CHECK(schedule()["pause_at_layer"]["enable"] == false);
        CHECK(schedule()["pause_at_layer"]["layer"] == 42);
        client.gcode_script("set_pause_at_layer layer=7");
        CHECK(schedule()["pause_at_layer"]["enable"] == true);
        CHECK(schedule()["pause_at_layer"]["layer"] == 7);
    }
    SECTION("CANCEL_PRINT clears both") {
        client.gcode_script("SET_PAUSE_NEXT_LAYER");
        client.gcode_script("SET_PAUSE_AT_LAYER LAYER=9");
        client.gcode_script("CANCEL_PRINT");
        CHECK(schedule()["pause_next_layer"]["enable"] == false);
        CHECK(schedule()["pause_at_layer"]["enable"] == false);
    }
}

TEST_CASE_METHOD(MockLayerPauseFixture, "Mock layer pause: AT_LAYER fires only on its exact layer",
                 "[layer_pause][mock]") {
    client.gcode_script("SET_PAUSE_AT_LAYER LAYER=5");
    client.layer_pause_on_layer(3, 4);
    CHECK(schedule()["pause_at_layer"]["enable"] == true);
    // Skipping past it never fires, and it stays armed, as client.cfg does.
    client.layer_pause_on_layer(4, 6);
    CHECK(schedule()["pause_at_layer"]["enable"] == true);
    client.layer_pause_on_layer(4, 5);
    CHECK(schedule()["pause_at_layer"]["enable"] == false);
}

TEST_CASE("Mock layer pause: HELIX_MOCK_LAYER_PAUSE=0 removes the macros", "[layer_pause][mock]") {
    helix::ScopedEnv env("HELIX_MOCK_LAYER_PAUSE", "0");
    CHECK_FALSE(MoonrakerClientMock::layer_pause_macros_loaded());
    json objects = json::array();
    MoonrakerClientMock::append_layer_pause_objects(objects);
    CHECK(objects.empty());
}
