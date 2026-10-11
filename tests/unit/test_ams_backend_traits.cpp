// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ams_backend_traits.cpp
 * @brief Each AmsBackendMock persona answers the constant capability questions
 *        exactly as the real backend it stands in for.
 *
 * The mock is what every `--test` run drives. Its personas take their traits
 * from the real backends' kTraits, so a capability a real backend changes
 * reaches the mock with no second edit. These cases fail the moment the two
 * disagree on any field.
 */

#include "ams_backend_ad5x_ifs.h"
#include "ams_backend_afc.h"
#include "ams_backend_happy_hare.h"
#include "ams_backend_mock.h"
#include "ams_backend_snapmaker.h"
#include "ams_backend_toolchanger.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

/// The mock's traits with the fields it answers from itself copied from @p real,
/// so the comparison covers every other field. Environment data is faked for
/// every persona (HELIX_MOCK_AMS_ENV); the mock has no firmware store, so spool
/// persistence is always ToolState's job; consumption tracking is a test hook.
BackendTraits comparable(const AmsBackendMock& mock, const BackendTraits& real) {
    BackendTraits t = mock.traits();
    t.has_environment_sensors = real.has_environment_sensors;
    t.has_firmware_spool_persistence = real.has_firmware_spool_persistence;
    t.tracks_consumption_natively = real.tracks_consumption_natively;
    return t;
}

} // namespace

TEST_CASE("Mock personas answer their real backend's traits", "[ams][mock][traits]") {
    SECTION("Happy Hare (default persona)") {
        AmsBackendMock mock(4);
        CHECK(comparable(mock, AmsBackendHappyHare::kTraits) == AmsBackendHappyHare::kTraits);
    }
    SECTION("AFC") {
        AmsBackendMock mock(4);
        mock.set_afc_mode(true);
        CHECK(comparable(mock, AmsBackendAfc::kTraits) == AmsBackendAfc::kTraits);
    }
    SECTION("Tool changer") {
        AmsBackendMock mock(4);
        mock.set_tool_changer_mode(true);
        CHECK(comparable(mock, AmsBackendToolChanger::kTraits) == AmsBackendToolChanger::kTraits);
    }
    SECTION("AD5X IFS") {
        AmsBackendMock mock(4);
        mock.set_ifs_mode(true);
        CHECK(comparable(mock, AmsBackendAd5xIfs::kTraits) == AmsBackendAd5xIfs::kTraits);
    }
    SECTION("Snapmaker U1") {
        AmsBackendMock mock(4);
        mock.set_snapmaker_mode(true);
        CHECK(comparable(mock, AmsBackendSnapmaker::kTraits) == AmsBackendSnapmaker::kTraits);
    }
}

TEST_CASE("Mock environment sensors follow the configured environment mode",
          "[ams][mock][traits]") {
    AmsBackendMock mock(4);
    mock.set_environment_mode("passive");
    CHECK(mock.has_environment_sensors());
    mock.set_environment_mode("none");
    CHECK_FALSE(mock.has_environment_sensors());
}

TEST_CASE("A backend that sets no traits answers the base defaults", "[ams][traits]") {
    const BackendTraits t{};
    CHECK(t.has_physical_tray);
    CHECK(t.slot_status_tracks_filament);
    CHECK(t.allows_implicit_chaining);
    CHECK_FALSE(t.supports_auto_heat_on_load);
    CHECK_FALSE(t.holds_optimistic_action);
    CHECK_FALSE(t.has_per_slot_loaded_authority);
}

TEST_CASE("Real kTraits keep the answers the UI branches on", "[ams][traits]") {
    // The persona cases above compare the mock with these structs, so they
    // cannot catch a wrong value here. These pin the ones a lane-per-tool
    // system must keep.
    CHECK(AmsBackendHappyHare::kTraits.has_physical_tray);
    CHECK_FALSE(AmsBackendHappyHare::kTraits.recovers_filament_on_resume);
    CHECK_FALSE(AmsBackendHappyHare::kTraits.should_suppress_idle_runout_modal);
    CHECK_FALSE(AmsBackendHappyHare::kTraits.supports_batch_filament_ops);
    CHECK_FALSE(AmsBackendHappyHare::kTraits.allows_implicit_chaining);
    CHECK_FALSE(AmsBackendSnapmaker::kTraits.has_physical_tray);
    CHECK(AmsBackendSnapmaker::kTraits.supports_batch_filament_ops);
    // Only a backend silent until its firmware starts holds the UI's action.
    CHECK(AmsBackendHappyHare::kTraits.holds_optimistic_action);
    CHECK_FALSE(AmsBackendAfc::kTraits.holds_optimistic_action);
    CHECK_FALSE(AmsBackendSnapmaker::kTraits.holds_optimistic_action);
}

TEST_CASE("The mock leaves spool persistence to ToolState in every persona",
          "[ams][mock][traits]") {
    AmsBackendMock mock(4);
    mock.set_afc_mode(true);
    REQUIRE(AmsBackendAfc::kTraits.has_firmware_spool_persistence);
    CHECK_FALSE(mock.has_firmware_spool_persistence());
}

TEST_CASE("Mock AFC load leaves only the new lane loaded", "[ams][mock][traits]") {
    AmsBackendMock mock(4);
    mock.set_afc_mode(true);
    mock.set_operation_delay(0);
    REQUIRE(mock.start());
    REQUIRE(mock.has_per_slot_loaded_authority());

    const int prior = mock.get_current_slot();
    REQUIRE(prior >= 0);
    REQUIRE(mock.slot_is_actively_loaded(prior));

    int target = -1;
    for (int i = 0; i < mock.get_system_info().total_slots; ++i) {
        if (i != prior && mock.get_slot_info(i).status == SlotStatus::AVAILABLE) {
            target = i;
            break;
        }
    }
    REQUIRE(target >= 0);

    REQUIRE(mock.load_filament(target).success());
    mock.wait_for_operation_thread();

    CHECK(mock.slot_is_actively_loaded(target));
    CHECK_FALSE(mock.slot_is_actively_loaded(prior));
    mock.stop();
}
