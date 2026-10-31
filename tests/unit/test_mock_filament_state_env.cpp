// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_mock_filament_state_env.cpp
 * @brief HELIX_MOCK_FILAMENT_STATE reaches both ways the mock reports a filament
 *        sensor: the initial status frame and printer.objects.subscribe (#1823).
 */

#include "../helix_test_fixture.h"
#include "../test_helpers/moonraker_client_mock_test_access.h"
#include "../test_helpers/scoped_env.h"
#include "moonraker_client_mock.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

constexpr const char* kEncoder = "filament_motion_sensor encoder";
constexpr const char* kRunout = "filament_switch_sensor runout";
constexpr const char* kPlain = "filament_switch_sensor plain";

void check_fields(const json& status, const char* object, bool detected, bool enabled) {
    INFO(object);
    REQUIRE(status.contains(object));
    CHECK(status[object]["filament_detected"].get<bool>() == detected);
    CHECK(status[object]["enabled"].get<bool>() == enabled);
}

} // namespace

TEST_CASE_METHOD(HelixTestFixture, "mock filament sensors follow HELIX_MOCK_FILAMENT_STATE",
                 "[mock][motion][1823]") {
    ScopedEnv sensors("HELIX_MOCK_FILAMENT_SENSORS", "motion:encoder,switch:runout,switch:plain");
    ScopedEnv state("HELIX_MOCK_FILAMENT_STATE", "encoder:disabled,runout:empty");
    MoonrakerClientMock client;

    SECTION("the initial status frame") {
        json frame;
        client.register_notify_update([&frame](const json& notification) {
            const auto params = notification.find("params");
            if (params != notification.end() && params->is_array() && !params->empty() &&
                (*params)[0].contains(kEncoder)) {
                frame = (*params)[0];
            }
        });
        MoonrakerClientMockTestAccess::dispatch_initial_state(client);
        check_fields(frame, kEncoder, true, false);
        check_fields(frame, kRunout, false, true);
        check_fields(frame, kPlain, true, true);
    }

    SECTION("printer.objects.subscribe") {
        json response;
        client.send_jsonrpc(
            "printer.objects.subscribe",
            json{{"objects",
                  json::object({{kEncoder, nullptr}, {kRunout, nullptr}, {kPlain, nullptr}})}},
            [&response](const json& r) { response = r; });
        REQUIRE(response.contains("result"));
        const json& status = response["result"]["status"];
        check_fields(status, kEncoder, true, false);
        check_fields(status, kRunout, false, true);
        check_fields(status, kPlain, true, true);
    }
}
