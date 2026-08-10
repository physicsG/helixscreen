// SPDX-License-Identifier: GPL-3.0-or-later

// AmsBackendMultiAce — the U1's four heads (unit 0, inherited untouched from
// AmsBackendSnapmaker) plus 1-4 Anycubic ACE units as units 1..N.
//
// The `ace` frames here are the real ones: tests/fixtures/snapmaker_u1/
// u1-multiace-head-mode-idle.json, captured from a live U1 on 2026-08-10 with
// one ACE Pro 2 in head mode feeding T3 while T0-T2 sit on stock feeders.

#include "../helix_test_fixture.h"
#include "ams_backend_multiace.h"
#include "ams_types.h"

#include <algorithm>
#include <fstream>

#include "../catch_amalgamated.hpp"

using json = nlohmann::json;
using HeadSource = AmsBackendMultiAce::HeadSource;

namespace {

/// The captured `ace` object, or a compact stand-in if the fixture is missing.
/// Loaded from disk rather than inlined so the test moves with the hardware:
/// re-capture the file and the expectations below are re-checked against it.
json live_ace_object() {
    std::ifstream f("tests/fixtures/snapmaker_u1/u1-multiace-head-mode-idle.json");
    REQUIRE(f.good()); // fixture must be committed alongside this test
    json fixture;
    f >> fixture;
    return fixture["status"]["ace"];
}

class CapturingMultiAce : public AmsBackendMultiAce {
  public:
    // running_ must be set or the filament ops answer not_connected at
    // check_preconditions() and we would assert the gate, not the command.
    CapturingMultiAce() : AmsBackendMultiAce(nullptr, nullptr) {
        running_.store(true);
    }
    std::vector<std::string> captured_gcodes;
    AmsError execute_gcode(const std::string& gcode) override {
        captured_gcodes.push_back(gcode);
        return AmsErrorHelper::success();
    }
    using AmsBackendMultiAce::handle_status_update;
};

json wrap(const json& ace) {
    return json{{"ace", ace}};
}

} // namespace

TEST_CASE_METHOD(HelixTestFixture, "multiACE reads head source kinds off the live frame",
                 "[ams][multiace]") {
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(live_ace_object()));

    // The captured machine: T0-T2 on stock feeders, only T3 fed by the ACE.
    CHECK(backend.head_source_kind(0) == HeadSource::FEEDER);
    CHECK(backend.head_source_kind(1) == HeadSource::FEEDER);
    CHECK(backend.head_source_kind(2) == HeadSource::FEEDER);
    CHECK(backend.head_source_kind(3) == HeadSource::ACE);
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE does not read head_ace as the ACE-fed signal",
                 "[ams][multiace]") {
    // The trap this test pins: head_ace names an ACE index for EVERY head — on
    // the live machine {0:0,1:1,2:2,3:0} — while only head 3 is actually ACE-fed.
    // Using it to decide would make all four heads take the ACE command path.
    CapturingMultiAce backend;
    json ace = json{{"mode", "head"},
                    {"device_count", 1},
                    {"head_ace", {{"0", 0}, {"1", 1}, {"2", 2}, {"3", 0}}},
                    {"head_feeder", {{"0", true}, {"1", true}, {"2", true}, {"3", false}}},
                    {"head_manual", {{"0", false}, {"1", false}, {"2", false}, {"3", false}}}};
    backend.handle_status_update(wrap(ace));

    CHECK(backend.head_source_kind(0) == HeadSource::FEEDER);
    CHECK(backend.head_source_kind(3) == HeadSource::ACE);
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE head maps are keyed by string, not int",
                 "[ams][multiace]") {
    // multiACE renders the head index as a JSON object KEY, so it is "0".."3".
    // An int-keyed lookup finds nothing and every head silently reads FEEDER
    // (or UNKNOWN) — the failure is invisible without this assertion.
    CapturingMultiAce backend;
    json ace = json{{"mode", "head"},
                    {"device_count", 1},
                    {"head_feeder", {{"0", false}, {"1", false}, {"2", false}, {"3", false}}},
                    {"head_manual", {{"0", true}, {"1", false}, {"2", false}, {"3", false}}}};
    backend.handle_status_update(wrap(ace));

    CHECK(backend.head_source_kind(0) == HeadSource::MANUAL);
    CHECK(backend.head_source_kind(1) == HeadSource::ACE);
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE reads the real notify_status_update wrapper",
                 "[ams][multiace]") {
    // Live notifications arrive as {"method":..., "params":[{status}, ts]}; only
    // the initial query response is the bare object every other test here uses.
    // Getting this wrong is invisible to those tests AND to the logs: the
    // backend constructs, subscribes and then quietly behaves like the plain
    // Snapmaker one. Caught on hardware, so it is pinned here.
    CapturingMultiAce backend;
    json notification = json{{"method", "notify_status_update"},
                             {"params", json::array({json{{"ace", live_ace_object()}}, 12345.0})}};
    backend.handle_status_update(notification);

    CHECK(backend.head_source_kind(3) == HeadSource::ACE);
    CHECK(backend.get_system_info().units.size() == 2);
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE maps the seated ACE slot to a head", "[ams][multiace]") {
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(live_ace_object()));

    auto seated = backend.seated_source(3);
    REQUIRE(seated.has_value());
    CHECK(seated->ace_index == 0);
    CHECK(seated->slot == 0);
    // Unit 0 is the U1 itself, so ACE n is global unit n+1.
    CHECK(seated->unit_index == 1);

    // Feeder heads have no ACE source seated.
    CHECK_FALSE(backend.seated_source(0).has_value());
    CHECK_FALSE(backend.seated_source(2).has_value());
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE adds ACE units alongside the U1's heads",
                 "[ams][multiace]") {
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(live_ace_object()));

    const auto info = backend.get_system_info();
    REQUIRE(info.units.size() == 2); // unit 0 = U1 SnapSwap, unit 1 = the ACE

    // Unit 0 is the inherited U1 and must be left exactly as the base built it.
    CHECK(info.units[0].slot_count == 4);
    CHECK(info.units[0].topology == PathTopology::PARALLEL);

    const auto& ace_unit = info.units[1];
    CHECK(ace_unit.unit_index == 1);
    // protocol v2 is the ACE 2 Pro — multiACE's own reference names the two
    // generations "ACE Pro v1" and "ACE 2 Pro v2", digit in the middle.
    CHECK(ace_unit.display_name == "ACE 2 Pro");
    CHECK(ace_unit.slot_count == 4);
    CHECK(ace_unit.connected);
    CHECK(ace_unit.first_slot_global_index == 4);
    // head mode: one ACE binds to one head and all four bays feed it — a hub.
    CHECK(ace_unit.topology == PathTopology::HUB);
    // gate_status was [1,0,0,0] at capture: one spool loaded, three empty bays.
    CHECK(ace_unit.slots[0].status == SlotStatus::AVAILABLE);
    CHECK(ace_unit.slots[1].status == SlotStatus::EMPTY);
    // In head mode every bay of this ACE reaches the head it is bound to (T3).
    CHECK(ace_unit.slots[0].mapped_tool == 3);
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE says the ACE owns an ACE-fed head's spool identity",
                 "[ams][multiace]") {
    // The spool at an ACE-fed head is described by the ACE. Editing it on the
    // head writes print_task_config, which the ACE overwrites on its next
    // report — two sources of truth for one spool. The slot menu drops its edit
    // actions on the strength of this and offers a route to the owner instead.
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(live_ace_object()));

    auto owner = backend.slot_identity_owner_unit(3);
    REQUIRE(owner.has_value());
    CHECK(*owner == 1); // the ACE is global unit 1

    // Feeder heads describe themselves.
    CHECK_FALSE(backend.slot_identity_owner_unit(0).has_value());
    CHECK_FALSE(backend.slot_identity_owner_unit(2).has_value());
    // So does an ACE bay: it IS the source.
    CHECK_FALSE(backend.slot_identity_owner_unit(4).has_value());
    CHECK_FALSE(backend.slot_identity_owner_unit(-1).has_value());
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE counts spools, not slots", "[ams][multiace]") {
    // A 4-head U1 with one 4-bay ACE has EIGHT addressable slots but SEVEN
    // spools: T3 and ACE bay 1 are the same physical spool seen from two sides.
    // Counting slots advertised 8 on the home widget and 4 on the U1's card.
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(live_ace_object()));

    const auto owned = backend.owned_spool_slots();
    CHECK(owned.size() == 7);
    // The head that is fed from elsewhere is the one dropped — not whichever
    // slot happens to sit last, which is what a bare count would have removed.
    CHECK(std::find(owned.begin(), owned.end(), 3) == owned.end());
    CHECK(std::find(owned.begin(), owned.end(), 7) != owned.end());

    CHECK(backend.unit_spool_slot_count(0) == 3); // U1: four heads, one borrowed
    CHECK(backend.unit_spool_slot_count(1) == 4); // ACE: all four bays are its own
    // total_slots is the INDEXING bound and must not move — every slot stays
    // addressable, loadable and unloadable.
    CHECK(backend.get_system_info().total_slots == 8);
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE answers per-unit topology, not the system one",
                 "[ams][multiace]") {
    // compute_system_tool_layout() and the unit detail both prefer the backend's
    // get_unit_topology() over AmsUnit::topology, and the base implementation
    // falls back to the SYSTEM answer — PARALLEL here. Populating the struct
    // alone left the ACE drawing as a parallel fan instead of a combiner.
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(live_ace_object()));

    CHECK(backend.get_unit_topology(0) == PathTopology::PARALLEL); // the U1's heads
    CHECK(backend.get_unit_topology(1) == PathTopology::HUB);      // ACE in head mode
    // Out of range must not read past the vector.
    CHECK(backend.get_unit_topology(9) == PathTopology::PARALLEL);
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE multi mode gives each ACE bay its own head",
                 "[ams][multiace]") {
    // In multi mode slot s feeds head s, so the unit is a parallel fan rather
    // than a hub. This is the shape Gordian's machine is NOT in, which is
    // exactly why it needs a test.
    CapturingMultiAce backend;
    json ace = json{{"mode", "multi"},
                    {"device_count", 1},
                    {"head_feeder", {{"0", false}, {"1", false}, {"2", false}, {"3", false}}},
                    {"aces", json::array({json{{"idx", 0},
                                               {"connected", true},
                                               {"gate_status", json::array({1, 1, 0, 0})}}})}};
    backend.handle_status_update(wrap(ace));

    const auto info = backend.get_system_info();
    REQUIRE(info.units.size() == 2);
    CHECK(info.units[1].topology == PathTopology::PARALLEL);
    CHECK(info.units[1].slots[0].mapped_tool == 0);
    CHECK(info.units[1].slots[2].mapped_tool == 2);
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE unplugging a unit drops its unit card",
                 "[ams][multiace]") {
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(live_ace_object()));
    REQUIRE(backend.get_system_info().units.size() == 2);

    // device_count falling must remove the unit, not leave a stale card drawing
    // hardware that is no longer attached.
    backend.handle_status_update(wrap(json{{"device_count", 0}}));
    CHECK(backend.get_system_info().units.size() == 1);
    CHECK(backend.get_system_info().units[0].slot_count == 4); // U1 untouched
}

// ============================================================================
// Dispatch — the point of the whole backend (plan §10.2)
// ============================================================================

TEST_CASE_METHOD(HelixTestFixture, "multiACE drives an ACE-fed head with the ACE commands",
                 "[ams][multiace][unload]") {
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(live_ace_object()));

    SECTION("unload") {
        // The native path does move the filament here, but terminates at
        // preload_finish because the ACE performs the retract — the UI then
        // waits for unload_finish forever.
        auto err = backend.unload_filament(3);
        CHECK(err.success());
        REQUIRE(backend.captured_gcodes.size() == 1);
        CHECK(backend.captured_gcodes[0] == "ACE_UNLOAD_HEAD HEAD=3");
    }
    SECTION("load") {
        auto err = backend.load_filament(3);
        CHECK(err.success());
        REQUIRE(backend.captured_gcodes.size() == 1);
        CHECK(backend.captured_gcodes[0] == "ACE_LOAD_HEAD HEAD=3");
    }
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE leaves feeder heads on the native U1 path",
                 "[ams][multiace][unload]") {
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(live_ace_object()));

    // T0 is on its stock feeder: the inherited FEED_AUTO path is the only one
    // that exists for it, and an ACE command would be addressed to hardware
    // that does not feed this head.
    auto err = backend.load_filament(0);
    CHECK(err.success());
    REQUIRE(backend.captured_gcodes.size() == 1);
    CHECK(backend.captured_gcodes[0].find("ACE_") == std::string::npos);
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE before any ace frame keeps the native path",
                 "[ams][multiace]") {
    // UNKNOWN is not FEEDER, but it must behave like it for dispatch: guessing
    // the ACE path for a head we have no information about would address a unit
    // that may not exist. Nothing has been parsed here at all.
    CapturingMultiAce backend;
    CHECK(backend.head_source_kind(3) == HeadSource::UNKNOWN);

    auto err = backend.load_filament(3);
    CHECK(err.success());
    REQUIRE(backend.captured_gcodes.size() == 1);
    CHECK(backend.captured_gcodes[0].find("ACE_") == std::string::npos);
}
