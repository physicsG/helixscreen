// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_chamber_mock_dragonbreath.cpp
 * @brief Mock-client materialization of the dragonbreath chamber-heater trio
 *        (issue #1290, task 6).
 *
 * HELIX_MOCK_OBJECTS="heater_generic dragonbreath dragonbreath
 * output_pin dragonbreath_filter" must yield:
 *   1. an object list with all three objects (tokenizer: `output_pin` is a
 *      prefix; a bare `dragonbreath` diagnostics token starts a NEW object
 *      instead of appending to the completed heater),
 *   2. discovery resolving the chamber heater to `heater_generic
 *      dragonbreath` via the backend registry — including the mock's
 *      chamber-key cache, which must consult the registry, not
 *      find("chamber"),
 *   3. status frames carrying the diagnostics object (nominal payload:
 *      fault false, fan_percent 0, ptc_temp > 0) plus the heater object with
 *      temperature/target,
 *   4. a configfile answer of max_temp 75 for the heater section,
 *   5. SET_PIN round-trip on the filter pin,
 *   6. a HELIX_MOCK_DRAGONBREATH_FAULT=1 hook flipping fault + fault_reason.
 */

#include "../helix_test_fixture.h"
#include "../test_helpers/moonraker_client_mock_test_access.h"
#include "moonraker_client_mock.h"

#include <algorithm>
#include <string>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

/// setenv for the test body, unset on scope exit — the mock reads these in
/// populate_capabilities() (constructor and discover_printer), so they must
/// be set before construction and stay set for the client's lifetime.
class ScopedEnv {
  public:
    ScopedEnv(const char* name, const char* value) : name_(name) {
        setenv(name_.c_str(), value, 1);
    }
    ~ScopedEnv() {
        unsetenv(name_.c_str());
    }

  private:
    std::string name_;
};

constexpr const char* TRIO_ENV =
    "heater_generic dragonbreath dragonbreath output_pin dragonbreath_filter";

bool object_list_contains(const json& response, const std::string& object) {
    if (!response.contains("result") || !response["result"].contains("objects")) {
        return false;
    }
    const auto& objects = response["result"]["objects"];
    return std::find(objects.begin(), objects.end(), json(object)) != objects.end();
}

json query_object_list(MoonrakerClientMock& client) {
    json response;
    client.send_jsonrpc("printer.objects.list", json::object(),
                        [&response](const json& r) { response = r; });
    return response;
}

} // namespace

TEST_CASE_METHOD(HelixTestFixture, "mock materializes dragonbreath trio", "[chamber][mock]") {
    ScopedEnv objects_env("HELIX_MOCK_OBJECTS", TRIO_ENV);
    MoonrakerClientMock client;

    SECTION("object list contains exactly the three parsed objects") {
        const json response = query_object_list(client);
        CHECK(object_list_contains(response, "heater_generic dragonbreath"));
        CHECK(object_list_contains(response, "dragonbreath"));
        CHECK(object_list_contains(response, "output_pin dragonbreath_filter"));
        // The two ways the accumulator can mis-parse: appending the bare
        // diagnostics token to the heater, and flushing a bare prefix.
        CHECK_FALSE(object_list_contains(response, "heater_generic dragonbreath dragonbreath"));
        CHECK_FALSE(object_list_contains(response, "output_pin"));
    }

    SECTION("discovery resolves chamber heater via backend registry") {
        const PrinterDiscovery hw = client.hardware();
        REQUIRE(hw.has_chamber_heater());
        CHECK(hw.chamber_heater_name() == "heater_generic dragonbreath");
        CHECK(hw.chamber_heater_backend_id() == "dragonbreath");
        CHECK(hw.chamber_diagnostics_object() == "dragonbreath");
        CHECK(hw.chamber_filter_fan_pin() == "output_pin dragonbreath_filter");
        // The registry-driven override must have retired the default
        // VORON_24 "heater_generic chamber" (keyword 100 beats dragonbreath's
        // 95, so a stale entry would still WIN the discovery pick).
        const auto& heaters = hw.heaters();
        CHECK(std::find(heaters.begin(), heaters.end(), "heater_generic chamber") == heaters.end());
        // The status-key cache follows the registry, not find("chamber").
        CHECK(MoonrakerClientMockTestAccess::chamber_heater_status_key(client) ==
              "heater_generic dragonbreath");
    }

    SECTION("first status frame carries diagnostics + heater") {
        json frame;
        client.register_notify_update(
            [&frame](const json& notification) { frame = notification["params"][0]; });
        MoonrakerClientMockTestAccess::dispatch_initial_state(client);

        REQUIRE(frame.is_object());
        REQUIRE(frame.contains("dragonbreath"));
        const json& diag = frame["dragonbreath"];
        CHECK(diag["fault"].get<bool>() == false);
        CHECK(diag["fan_percent"].get<int>() == 0);
        CHECK(diag["ptc_temp"].get<double>() > 0.0);
        CHECK(diag["inhibited"].get<bool>() == false);
        CHECK(diag["connected"].get<bool>() == true);
        CHECK(diag["mode"].get<std::string>() == "off"); // no target set yet
        // fault_reason is JSON null in the nominal frame
        CHECK((diag["fault_reason"].is_null() || diag["fault_reason"].get<std::string>().empty()));

        REQUIRE(frame.contains("heater_generic dragonbreath"));
        CHECK(frame["heater_generic dragonbreath"]["temperature"].get<double>() > 0.0);
        CHECK(frame["heater_generic dragonbreath"].contains("target"));

        REQUIRE(frame.contains("output_pin dragonbreath_filter"));
        CHECK(frame["output_pin dragonbreath_filter"]["value"].get<double>() == 0.0);
    }

    SECTION("configfile answers max_temp 75 for the heater section") {
        json response;
        client.send_jsonrpc(
            "printer.objects.query",
            json{{"objects", json::object({{"configfile", json::array({"config"})}})}},
            [&response](const json& r) { response = r; });
        REQUIRE(response.contains("result"));
        REQUIRE(response["result"]["status"].contains("configfile"));
        const json& config = response["result"]["status"]["configfile"]["config"];
        REQUIRE(config.contains("heater_generic dragonbreath"));
        CHECK(config["heater_generic dragonbreath"]["max_temp"].get<double>() == 75.0);
    }

    SECTION("SET_PIN toggles the filter pin in subsequent frames") {
        json frame;
        client.register_notify_update(
            [&frame](const json& notification) { frame = notification["params"][0]; });

        REQUIRE(client.gcode_script("SET_PIN PIN=dragonbreath_filter VALUE=1") == 0);
        REQUIRE(frame.contains("output_pin dragonbreath_filter"));
        CHECK(frame["output_pin dragonbreath_filter"]["value"].get<double>() == 1.0);

        REQUIRE(client.gcode_script("SET_PIN PIN=dragonbreath_filter VALUE=0") == 0);
        CHECK(frame["output_pin dragonbreath_filter"]["value"].get<double>() == 0.0);

        // And the synthesized diagnostics frame follows the pin (fan_percent).
        frame = json();
        MoonrakerClientMockTestAccess::dispatch_initial_state(client);
        REQUIRE(frame.contains("dragonbreath"));
        CHECK(frame["dragonbreath"]["fan_percent"].get<int>() == 0);
    }

    SECTION("SET_HEATER_TEMPERATURE uses the bare backend heater name") {
        json frame;
        client.register_notify_update(
            [&frame](const json& notification) { frame = notification["params"][0]; });

        REQUIRE(client.gcode_script("SET_HEATER_TEMPERATURE HEATER=dragonbreath TARGET=45") == 0);
        REQUIRE(frame.contains("heater_generic dragonbreath"));
        CHECK(frame["heater_generic dragonbreath"]["target"].get<double>() == 45.0);

        // The diagnostics mode field tracks the target.
        frame = json();
        MoonrakerClientMockTestAccess::dispatch_initial_state(client);
        REQUIRE(frame.contains("dragonbreath"));
        CHECK(frame["dragonbreath"]["mode"].get<std::string>() == "power_on");
    }

    SECTION("set_heaters rebuild keeps backend surfaces answering") {
        // rebuild_hardware_from_lists() re-parses from the discovery lists,
        // which cannot express the bare diagnostics object or the filter pin
        // (parse_objects classifies neither). Without the rebuild preserving
        // them, chamber status emission and objects.list silently go dark.
        client.set_heaters({"heater_bed", "extruder", "heater_generic dragonbreath"});

        const json response = query_object_list(client);
        CHECK(object_list_contains(response, "heater_generic dragonbreath"));
        CHECK(object_list_contains(response, "dragonbreath"));
        CHECK(object_list_contains(response, "output_pin dragonbreath_filter"));

        json frame;
        client.register_notify_update(
            [&frame](const json& notification) { frame = notification["params"][0]; });
        MoonrakerClientMockTestAccess::dispatch_initial_state(client);
        REQUIRE(frame.contains("dragonbreath"));
        CHECK(frame["dragonbreath"]["ptc_temp"].get<double>() > 0.0);
        CHECK(frame.contains("output_pin dragonbreath_filter"));
    }
}

TEST_CASE_METHOD(HelixTestFixture, "mock dragonbreath fault hook", "[chamber][mock]") {
    ScopedEnv objects_env("HELIX_MOCK_OBJECTS", TRIO_ENV);
    ScopedEnv fault_env("HELIX_MOCK_DRAGONBREATH_FAULT", "1");
    MoonrakerClientMock client;

    json frame;
    client.register_notify_update(
        [&frame](const json& notification) { frame = notification["params"][0]; });
    MoonrakerClientMockTestAccess::dispatch_initial_state(client);

    REQUIRE(frame.contains("dragonbreath"));
    CHECK(frame["dragonbreath"]["fault"].get<bool>() == true);
    CHECK_FALSE(frame["dragonbreath"]["fault_reason"].get<std::string>().empty());
}

TEST_CASE_METHOD(HelixTestFixture, "HELIX_MOCK_OBJECTS legacy shapes still parse",
                 "[chamber][mock]") {
    SECTION("temperature_fan chamber stays a single object and wins the chamber key") {
        ScopedEnv objects_env("HELIX_MOCK_OBJECTS", "temperature_fan chamber");
        MoonrakerClientMock client;

        const json response = query_object_list(client);
        CHECK(object_list_contains(response, "temperature_fan chamber"));
        // "chamber" appended to the prefix, not flushed as a bare object.
        CHECK_FALSE(object_list_contains(response, "chamber"));
        CHECK(MoonrakerClientMockTestAccess::chamber_heater_status_key(client) ==
              "temperature_fan chamber");
    }

    SECTION("default mock without env keeps heater_generic chamber") {
        MoonrakerClientMock client;
        CHECK(MoonrakerClientMockTestAccess::chamber_heater_status_key(client) ==
              "heater_generic chamber");
    }
}
