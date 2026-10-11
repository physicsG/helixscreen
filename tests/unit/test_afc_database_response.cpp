// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_afc_database_response.cpp
 * @brief Tests for AFC's server.database.get_item response handling
 *
 * send_jsonrpc hands its callback the full JSON-RPC envelope, so a
 * server.database.get_item payload lives at result.value — the shape documented
 * and unwrapped at moonraker_api.cpp:227. AFC read "value" off the top level
 * instead, so the guard was always false: the version was never applied,
 * has_lane_data_db_ stayed false, the lane_data query was never issued, and
 * parse_lane_data never ran (prestonbrown/helixscreen#1148).
 *
 * The mock does not implement server.database.get_item, so nothing exercised
 * these callbacks. These tests drive the parse directly.
 */

#include "../lvgl_test_fixture.h"
#include "test_helpers/afc_test_access.h"
#include "ams_backend_afc.h"
#include "ams_types.h"

#include <string>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using namespace helix;
using json = nlohmann::json;

namespace helix {

/// Reaches the response-application methods without needing a live client.
class AfcDatabaseResponseHelper : public AmsBackendAfc {
  public:
    AfcDatabaseResponseHelper() : AmsBackendAfc(nullptr, nullptr) {}

    bool apply_version(const json& response) {
        return AfcTestAccess::apply_afc_version_response(*this, response);
    }
    bool apply_lanes(const json& response) {
        return AfcTestAccess::apply_lane_data_response(*this, response);
    }
    static const json& item_value(const json& response) {
        return AfcTestAccess::database_item_value(response);
    }
    std::string version() const {
        return AfcTestAccess::afc_version(*this);
    }
};
} // namespace helix

namespace {

/// The envelope Moonraker actually sends for server.database.get_item.
json envelope(const char* ns, const json& value) {
    return {{"jsonrpc", "2.0"}, {"id", 1}, {"result", {{"namespace", ns}, {"value", value}}}};
}

} // namespace

// ============================================================================
// database_item_value — envelope level
// ============================================================================

TEST_CASE("database_item_value reads the payload from result.value", "[afc][database]") {
    const json resp = envelope("afc-install", {{"version", "1.0.40"}});
    const json& value = AfcDatabaseResponseHelper::item_value(resp);
    REQUIRE(value.is_object());
    CHECK(value.value("version", "") == "1.0.40");
}

// Strict about the envelope on purpose. A payload is an arbitrary object, so
// there is no reliable way to tell one from an envelope — the afc-install
// payload is {"version":…} and carries no "value" key to test. Accepting a bare
// object here would mean treating any unrecognised reply as a payload.
TEST_CASE("database_item_value does not treat a bare payload as an envelope", "[afc][database]") {
    const json bare = {{"version", "1.0.40"}};
    CHECK_FALSE(AfcDatabaseResponseHelper::item_value(bare).is_object());
}

TEST_CASE("database_item_value yields null when the key is missing", "[afc][database]") {
    CHECK_FALSE(AfcDatabaseResponseHelper::item_value(json::object()).is_object());
    // An envelope whose result carries no value is not a bare payload — do not
    // fall back to the top level and mistake the envelope itself for one.
    const json no_value = {{"jsonrpc", "2.0"}, {"result", {{"namespace", "afc-install"}}}};
    CHECK_FALSE(AfcDatabaseResponseHelper::item_value(no_value).is_object());
}

// ============================================================================
// Version application
// ============================================================================

TEST_CASE("AFC version is applied from a real database envelope", "[afc][database]") {
    AfcDatabaseResponseHelper afc;
    // Unset default. Note this is what version_at_least() has actually been
    // comparing against in the field, since no version was ever applied.
    REQUIRE(afc.version() == "unknown");

    REQUIRE(afc.apply_version(envelope("afc-install", {{"version", "1.0.40"}})));
    CHECK(afc.version() == "1.0.40");
}

// lane_data is NOT gated on a version any more. AFC deleted the code that wrote
// the afc-install namespace in its commit 7d20db7 (#451, 2025-06-16), so the
// version string is either absent or frozen at whatever it was before that date.
// A real BoxTurtle on 2026-07-26 reported "1.0.0" while its payload proved
// 1.0.32-era, and its lane_data namespace was fully populated — gating on the
// version threw that data away.
TEST_CASE("lane_data does not depend on the reported version", "[afc][database]") {
    AfcDatabaseResponseHelper old_afc;
    REQUIRE(old_afc.apply_version(envelope("afc-install", {{"version", "1.0.0"}})));
    CHECK(old_afc.version() == "1.0.0");

    // A stale/ancient version must not stop lane_data from being applied.
    const json lanes = {{"lane1", {{"material", "ASA"}, {"color", "#c1c1c1"}, {"spool_id", 127}}}};
    REQUIRE(old_afc.apply_lanes(envelope("lane_data", lanes)));
    CHECK(old_afc.get_slot_info(0).material == "ASA");
    CHECK(old_afc.get_slot_info(0).spoolman_id == 127);
}

TEST_CASE("a malformed version reply applies nothing", "[afc][database]") {
    AfcDatabaseResponseHelper afc;
    CHECK_FALSE(afc.apply_version(envelope("afc-install", json::object())));
    CHECK_FALSE(afc.apply_version(envelope("afc-install", {{"version", nullptr}})));
    CHECK_FALSE(afc.apply_version(envelope("afc-install", "not-an-object")));
    CHECK_FALSE(afc.apply_version(json::object()));
    CHECK(afc.version() == "unknown");
}

// ============================================================================
// Lane data application
// ============================================================================

// AFC writes colors with a leading '#' ("#c1c1c1"), which std::stoul cannot
// parse — every lane silently fell back to the default grey. Verified against a
// live BoxTurtle's lane_data namespace on 2026-07-26.
TEST_CASE("lane_data color accepts AFC's '#' prefix", "[afc][database]") {
    AfcDatabaseResponseHelper afc;
    const json lanes = {{"lane1", {{"color", "#c1c1c1"}}}, {"lane2", {{"color", "00ff00"}}}};
    REQUIRE(afc.apply_lanes(envelope("lane_data", lanes)));

    CHECK(afc.get_slot_info(0).color_rgb == 0xc1c1c1);
    // Bare hex (no '#') must keep working.
    CHECK(afc.get_slot_info(1).color_rgb == 0x00ff00);
}

TEST_CASE("lane_data leaves an unparseable color at the default", "[afc][database]") {
    AfcDatabaseResponseHelper afc;
    const json lanes = {{"lane1", {{"color", "not-a-color"}}}};
    REQUIRE(afc.apply_lanes(envelope("lane_data", lanes)));
    CHECK(afc.get_slot_info(0).color_rgb == AMS_DEFAULT_SLOT_COLOR);
}

// AFC's real lane_data payload carries NO status keys — no tool_loaded, loaded,
// available or empty. Defaulting to AVAILABLE in that case would clobber the
// status already derived from the AFC_stepper objects, marking empty lanes as
// loaded. Absent means unchanged, consistent with how we treat status deltas.
TEST_CASE("lane_data does not clobber slot status when it carries none",
          "[afc][database][regression]") {
    AfcDatabaseResponseHelper afc;

    // First pass establishes EMPTY via an explicit status key.
    REQUIRE(afc.apply_lanes(envelope("lane_data", {{"lane1", {{"empty", true}}}})));
    REQUIRE(afc.get_slot_info(0).status == SlotStatus::EMPTY);

    // Second pass is shaped like AFC's actual payload: metadata only.
    const json real_shape = {{"lane1",
                              {{"color", "#000000"},
                               {"material", "ASA"},
                               {"bed_temp", nullptr},
                               {"nozzle_temp", nullptr},
                               {"scan_time", ""},
                               {"td", ""},
                               {"lane", "0"},
                               {"spool_id", 86}}}};
    REQUIRE(afc.apply_lanes(envelope("lane_data", real_shape)));

    CHECK(afc.get_slot_info(0).status == SlotStatus::EMPTY);
    // ...while the metadata it DOES carry still lands.
    CHECK(afc.get_slot_info(0).material == "ASA");
    CHECK(afc.get_slot_info(0).spoolman_id == 86);
}

TEST_CASE("lane_data still applies an explicit status when present", "[afc][database]") {
    AfcDatabaseResponseHelper afc;
    REQUIRE(afc.apply_lanes(envelope("lane_data", {{"lane1", {{"tool_loaded", true}}}})));
    CHECK(afc.get_slot_info(0).status == SlotStatus::LOADED);

    AfcDatabaseResponseHelper afc2;
    REQUIRE(afc2.apply_lanes(envelope("lane_data", {{"lane1", {{"available", true}}}})));
    CHECK(afc2.get_slot_info(0).status == SlotStatus::AVAILABLE);
}

TEST_CASE("lane_data is parsed from a real database envelope", "[afc][database]") {
    AfcDatabaseResponseHelper afc;
    const json lanes = {{"lane1", {{"material", "PLA"}, {"color", "FF0000"}}},
                        {"lane2", {{"material", "PETG"}, {"color", "00FF00"}}}};
    REQUIRE(afc.apply_lanes(envelope("AFC", lanes)));
}

TEST_CASE("a non-object lane_data reply is rejected", "[afc][database]") {
    AfcDatabaseResponseHelper afc;
    CHECK_FALSE(afc.apply_lanes(envelope("AFC", nullptr)));
    CHECK_FALSE(afc.apply_lanes(envelope("AFC", json::array())));
    CHECK_FALSE(afc.apply_lanes(json::object()));
}
