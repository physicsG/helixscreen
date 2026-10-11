// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../test_helpers/mock_bypass.h"
#include "ams_backend_mock.h"

#include "../catch_amalgamated.hpp"

/**
 * @file test_ams_backend_mock_bypass.cpp
 * @brief Unit tests for AMS mock backend bypass mode functionality
 *
 * Tests the bypass mode feature which allows external spool feeding
 * directly to the toolhead, bypassing the MMU/hub system.
 */

TEST_CASE("AmsBackendMock bypass mode", "[ams][mock][bypass]") {
    helix::AmsBackendMock backend(4);
    backend.set_operation_delay(0); // Instant operations for tests
    REQUIRE(backend.start());
    REQUIRE(helix::test::unload_for_bypass(backend));

    SECTION("initially not in bypass mode") {
        REQUIRE_FALSE(backend.is_bypass_active());
        auto info = backend.get_system_info();
        REQUIRE(info.current_slot != -2);
    }

    SECTION("enable bypass sets current_slot to -2") {
        auto result = backend.enable_bypass();
        REQUIRE(result);
        REQUIRE(backend.is_bypass_active());

        auto info = backend.get_system_info();
        REQUIRE(info.current_slot == -2);
        REQUIRE(info.filament_loaded == true);
    }

    SECTION("disable bypass clears current_slot") {
        // First enable bypass
        auto enable_result = backend.enable_bypass();
        REQUIRE(enable_result);
        REQUIRE(backend.is_bypass_active());

        // Then disable
        auto disable_result = backend.disable_bypass();
        REQUIRE(disable_result);
        REQUIRE_FALSE(backend.is_bypass_active());

        auto info = backend.get_system_info();
        REQUIRE(info.current_slot == -1);
        REQUIRE(info.filament_loaded == false);
    }

    SECTION("disable bypass fails when not active") {
        REQUIRE_FALSE(backend.is_bypass_active());
        auto result = backend.disable_bypass();
        REQUIRE_FALSE(result);
    }

    SECTION("enable bypass fails when busy") {
        // Use a real (non-zero) operation delay so the load stays in-flight long
        // enough for the immediate busy check. With delay 0 the async completion
        // thread can finish in ~1ms and clear the busy state before enable_bypass
        // runs, making the rejection check flaky. cancel() ends it promptly so
        // the longer delay doesn't slow the test down.
        backend.set_operation_delay(500);

        // Start a load operation
        auto load_result = backend.load_filament(0);
        REQUIRE(load_result);

        // Try to enable bypass - should fail because busy
        auto bypass_result = backend.enable_bypass();
        REQUIRE_FALSE(bypass_result);

        // Cancel operation
        backend.cancel();
    }

    SECTION("get_filament_segment shows NOZZLE when bypass active") {
        backend.enable_bypass();
        REQUIRE(backend.get_filament_segment() == helix::PathSegment::NOZZLE);
    }

    SECTION("supports_bypass flag is set") {
        auto info = backend.get_system_info();
        REQUIRE(info.supports_bypass == true);
    }

    backend.stop();
}

TEST_CASE("AmsBackendMock bypass events", "[ams][mock][bypass][events]") {
    helix::AmsBackendMock backend(4);
    backend.set_operation_delay(0);

    bool state_changed = false;
    backend.set_event_callback([&](const std::string& event, const std::string&) {
        if (event == helix::AmsBackend::EVENT_STATE_CHANGED) {
            state_changed = true;
        }
    });

    REQUIRE(backend.start());
    REQUIRE(helix::test::unload_for_bypass(backend));
    state_changed = false; // Reset after start and unload events

    SECTION("enable bypass emits state changed event") {
        backend.enable_bypass();
        REQUIRE(state_changed);
    }

    SECTION("disable bypass emits state changed event") {
        backend.enable_bypass();
        state_changed = false;
        backend.disable_bypass();
        REQUIRE(state_changed);
    }

    backend.stop();
}

TEST_CASE("AmsBackendMock hardware bypass sensor", "[ams][mock][bypass][sensor]") {
    helix::AmsBackendMock backend(4);
    backend.set_operation_delay(0);
    REQUIRE(backend.start());
    REQUIRE(helix::test::unload_for_bypass(backend));

    SECTION("default is virtual bypass (no hardware sensor)") {
        auto info = backend.get_system_info();
        REQUIRE(info.has_hardware_bypass_sensor == false);
    }

    SECTION("can set hardware bypass sensor mode") {
        backend.set_has_hardware_bypass_sensor(true);
        auto info = backend.get_system_info();
        REQUIRE(info.has_hardware_bypass_sensor == true);
    }

    SECTION("can toggle back to virtual bypass") {
        backend.set_has_hardware_bypass_sensor(true);
        backend.set_has_hardware_bypass_sensor(false);
        auto info = backend.get_system_info();
        REQUIRE(info.has_hardware_bypass_sensor == false);
    }

    SECTION("bypass operations work regardless of sensor setting") {
        // Hardware sensor mode doesn't prevent enable/disable at backend level
        // (UI layer handles disabling the button)
        backend.set_has_hardware_bypass_sensor(true);

        auto enable_result = backend.enable_bypass();
        REQUIRE(enable_result);
        REQUIRE(backend.is_bypass_active());

        auto disable_result = backend.disable_bypass();
        REQUIRE(disable_result);
        REQUIRE_FALSE(backend.is_bypass_active());
    }

    SECTION("supports_bypass flag independent of sensor setting") {
        auto info1 = backend.get_system_info();
        REQUIRE(info1.supports_bypass == true);

        backend.set_has_hardware_bypass_sensor(true);
        auto info2 = backend.get_system_info();
        REQUIRE(info2.supports_bypass == true);
    }

    backend.stop();
}

TEST_CASE("AmsBackendMock refuses bypass with filament loaded where the hardware does",
          "[ams][mock][bypass]") {
    // Happy Hare and AFC do not chain an unload in front of bypass, so they
    // refuse it themselves while the toolhead holds filament.
    for (const bool afc : {false, true}) {
        helix::AmsBackendMock backend(4);
        backend.set_operation_delay(0);
        if (afc) {
            backend.set_afc_mode(true);
        }
        REQUIRE(backend.start());
        REQUIRE(backend.get_system_info().filament_loaded);
        REQUIRE_FALSE(backend.allows_implicit_chaining());

        const auto refused = backend.enable_bypass();
        CHECK(refused.result == helix::AmsResult::WRONG_STATE);
        CHECK_FALSE(backend.is_bypass_active());

        REQUIRE(helix::test::unload_for_bypass(backend));
        CHECK(backend.enable_bypass().success());
        CHECK(backend.is_bypass_active());
        backend.stop();
    }
}
