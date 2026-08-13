// SPDX-License-Identifier: GPL-3.0-or-later

// AmsBackendMultiAce — the U1's four heads (unit 0, inherited untouched from
// AmsBackendSnapmaker) plus 1-4 Anycubic ACE units as units 1..N.
//
// The `ace` frames here are the real ones: tests/fixtures/snapmaker_u1/
// u1-multiace-head-mode-idle.json, captured from a live U1 on 2026-08-10 with
// one ACE Pro 2 in head mode feeding T3 while T0-T2 sit on stock feeders.

#include "../helix_test_fixture.h"
#include "ams_backend_multiace.h"
#include "ams_backend_snapmaker.h"
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

/// Reaches the private static override parser. The friend declaration for this
/// name has been on AmsBackendMultiAce since it was written; this is the first
/// definition of it. Named at namespace scope, not inside the anonymous one, so
/// it matches the friend.
class MultiAceTestAccess {
  public:
    static AmsBackendMultiAce::OverrideMap parse_slot_overrides(const std::string& content) {
        return AmsBackendMultiAce::parse_slot_overrides(content);
    }
};

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

TEST_CASE_METHOD(HelixTestFixture, "multiACE keeps head sources across a partial frame",
                 "[ams][multiace]") {
    // Status frames are DELTAS. An `ace` frame that says nothing about head
    // sources must leave them alone. Treating absent as cleared wiped the
    // seating a second after it arrived, so the ACE's bays lost mapped_tool --
    // no tool badges, and the unit detail fell back to hub-only because it no
    // longer knew which head it fed. Invisible to a full-frame test.
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(live_ace_object()));
    REQUIRE(backend.seated_source(3).has_value());
    REQUIRE(backend.get_system_info().units.size() == 2);
    REQUIRE(backend.get_system_info().units[1].slots[0].mapped_tool == 3);

    // A frame carrying only an environment reading — exactly what Moonraker
    // sends between full updates.
    backend.handle_status_update(wrap(json{{"aces", json::array({json{{"idx", 0}, {"temp", 33}}})}}));

    CHECK(backend.seated_source(3).has_value());
    CHECK(backend.head_source_kind(3) == HeadSource::ACE);
    CHECK(backend.get_system_info().units[1].slots[0].mapped_tool == 3);

    // And the OTHER heads must not have drifted. The kind test reads "not manual,
    // not feeder => ACE", so running it against a frame that carried neither map
    // relabelled every head ACE-fed — which would send ACE_LOAD_HEAD to a head on
    // its stock feeder. It stayed invisible while everything downstream keyed on
    // head_seated_, which a partial frame leaves alone.
    CHECK(backend.head_source_kind(0) == HeadSource::FEEDER);
    CHECK(backend.head_source_kind(1) == HeadSource::FEEDER);
    CHECK(backend.head_source_kind(2) == HeadSource::FEEDER);
}

TEST_CASE_METHOD(HelixTestFixture,
                 "multiACE still dispatches the native path after a partial frame",
                 "[ams][multiace][unload]") {
    // The behavioural half of the drift above: a feeder head that had been
    // relabelled ACE-fed takes the ACE command path, which addresses hardware
    // that does not feed it.
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(live_ace_object()));
    backend.handle_status_update(
        wrap(json{{"aces", json::array({json{{"idx", 0}, {"temp", 33}}})}}));

    auto err = backend.load_filament(0);
    CHECK(err.success());
    REQUIRE(backend.captured_gcodes.size() == 1);
    CHECK(backend.captured_gcodes[0].find("ACE_") == std::string::npos);
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

TEST_CASE_METHOD(HelixTestFixture, "multiACE reads a bay's spool from the spool TABLE",
                 "[ams][multiace][spools]") {
    // What the user types into multiACE's web UI lands in a spool table, not in
    // slots[]. Those inline fields are filled from RFID and read EMPTY for every
    // hand-entered spool — which is exactly the live rig, and why the panel
    // showed none of what had been entered. Shape captured from the machine.
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(json{
        {"mode", "head"},
        {"device_count", 1},
        {"head_ace", {{"0", 0}, {"1", 1}, {"2", 2}, {"3", 0}}},
        {"head_feeder", {{"0", true}, {"1", true}, {"2", true}, {"3", false}}},
        // Empty inline fields, exactly as the hardware reports them.
        {"aces",
         json::array(
             {json{{"idx", 0},
                   {"connected", true},
                   {"protocol", "v2"},
                   {"gate_status", json::array({1, 1, 0, 1})},
                   {"slots",
                    json::array(
                        {json{{"index", 0}, {"material", ""}, {"color", json::array({0, 0, 0})}},
                         json{{"index", 1}, {"material", ""}}, json{{"index", 2}, {"material", ""}},
                         json{{"index", 3}, {"material", ""}}})}}})},
        {"spool_binding", {{"0_0", "15"}, {"0_1", "10"}, {"0_3", "16"}}},
        {"spools",
         {{"15", json{{"id", "15"},
                      {"material", "PETG"},
                      {"vendor", "Kingroon"},
                      {"color", "83AFFF"},
                      {"label", "SIlver"},
                      {"weight_g", 501.77},
                      {"sku", "SM15"},
                      {"spoolman_id", "15"}}},
          {"10", json{{"id", "10"},
                      {"material", "PETG"},
                      {"vendor", "Kingroon"},
                      {"color", "8FA7C8"},
                      {"label", "Gray"},
                      {"weight_g", 1000.0},
                      {"sku", "SM10"},
                      {"spoolman_id", "10"}}},
          {"16", json{{"id", "16"},
                      {"material", "PETG"},
                      {"vendor", "Kingroon"},
                      {"color", "C47053"},
                      {"label", "Orange"},
                      {"weight_g", 844.97},
                      {"sku", "SM17"},
                      {"spoolman_id", "17"}}}}}}));

    const auto info = backend.get_system_info();
    REQUIRE(info.units.size() == 2);
    const auto& ace = info.units[1];

    SECTION("a bound bay carries what the user entered") {
        CHECK(ace.slots[0].material == "PETG");
        CHECK(ace.slots[0].brand == "Kingroon");
        CHECK(ace.slots[0].spool_name == "SIlver");
        CHECK(ace.slots[0].color_rgb == 0x83AFFFu);
        CHECK(ace.slots[0].spoolman_id == 15);
        // Weight is NOT taken from multiACE: `weight_g` is its local copy of
        // Spoolman's figure, and SpoolmanManager fills remaining AND total from
        // spoolman_id. Taking one from each source made the percentage nonsense.
        CHECK(ace.slots[0].remaining_weight_g < 0.0f);
        CHECK(ace.slots[0].total_weight_g < 0.0f);
    }

    SECTION("each binding resolves to its OWN spool") {
        // "0_1" -> 10 and "0_3" -> 16: the key's slot half is what places it, so
        // an off-by-one here would silently swap two spools rather than fail.
        CHECK(ace.slots[1].spool_name == "Gray");
        CHECK(ace.slots[1].color_rgb == 0x8FA7C8u);
        CHECK(ace.slots[3].spool_name == "Orange");
        CHECK(ace.slots[3].color_rgb == 0xC47053u);
    }

    SECTION("an unbound bay stays empty") {
        CHECK(ace.slots[2].spool_name.empty());
        CHECK(ace.slots[2].material.empty());
    }

    SECTION("spoolman_id is read from a STRING") {
        // It is an int everywhere else in this codebase; here it is quoted.
        CHECK(ace.slots[3].spoolman_id == 17); // note: id 16, spoolman_id 17
    }

    SECTION("a frame with no binding leaves the table alone") {
        backend.handle_status_update(
            wrap(json{{"aces", json::array({json{{"idx", 0}, {"temp", 31}}})}}));
        CHECK(backend.get_system_info().units[1].slots[0].spool_name == "SIlver");
    }

    SECTION("unbinding a bay clears it") {
        // multiACE DELETES the key rather than nulling it, so the map has to be
        // replaced wholesale — merging would strand the binding forever.
        backend.handle_status_update(wrap(json{{"spool_binding", {{"0_1", "10"}}}}));
        const auto after = backend.get_system_info();
        CHECK(after.units[1].slots[1].spool_name == "Gray");
        CHECK(after.units[1].slots[0].spool_name.empty());
        CHECK(after.units[1].slots[3].spool_name.empty());
    }
}

TEST_CASE("multiACE parses the slot_overrides.json layer", "[ams][multiace][spools]") {
    // multiACE keeps per-bay identity in a FILE as well as in the spool table,
    // and its own web UI resolves from the file — every bay it reports comes back
    // source:"override". Critically the file covers bays the table does not: this
    // is the real content from the live machine, where bay 2 has a material and
    // colour but NO spool bound, and so read as empty until this layer was added.
    const std::string content = R"({
      "0_0": {"ace":0,"slot":0,"material":"PETG","brand":"Kingroon","subtype":"Basic","color":"#83AFFF"},
      "0_3": {"ace":0,"slot":3,"material":"PETG","brand":"Kingroon","subtype":"Basic","color":"#C47053"},
      "0_1": {"ace":0,"slot":1,"material":"PETG","brand":"Kingroon","subtype":"Basic","color":"#8FA7C8"},
      "0_2": {"ace":0,"slot":2,"material":"PETG","brand":"Generic","subtype":"Basic","color":"#632c2c"}
    })";

    const auto ov = MultiAceTestAccess::parse_slot_overrides(content);

    SECTION("every bay in the file is read, including one with no spool bound") {
        for (int s = 0; s < 4; ++s) {
            INFO("bay " << s);
            CHECK(ov[0][static_cast<size_t>(s)].set);
        }
        CHECK(ov[0][2].material == "PETG");
        CHECK(ov[0][2].brand == "Generic");
    }

    SECTION("colour drops the leading '#'") {
        // Three encodings for one concept across this backend: "#RRGGBB" here,
        // bare hex in the spool table, [r,g,b] in slots[].
        CHECK(ov[0][0].color_rgb == 0x83AFFFu);
        CHECK(ov[0][2].color_rgb == 0x632C2Cu); // lower-case in the file
        CHECK(ov[0][3].color_rgb == 0xC47053u);
    }

    SECTION("the KEY places the entry, not the ace/slot fields") {
        CHECK(ov[0][1].color_rgb == 0x8FA7C8u);
        CHECK(ov[0][3].color_rgb != ov[0][1].color_rgb);
    }

    SECTION("a bay absent from the file is left unset") {
        CHECK_FALSE(ov[1][0].set); // a second ACE nothing was said about
    }

    SECTION("malformed input yields nothing rather than throwing") {
        CHECK_FALSE(MultiAceTestAccess::parse_slot_overrides("not json")[0][0].set);
        CHECK_FALSE(MultiAceTestAccess::parse_slot_overrides("[]")[0][0].set);
        CHECK_FALSE(
            MultiAceTestAccess::parse_slot_overrides(R"({"bad":{"material":"PLA"}})")[0][0].set);
    }
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE numbers spools, not slots", "[ams][multiace]") {
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(live_ace_object()));

    // The captured rig: four U1 heads (global 0-3) with T3 fed from ACE bay 0,
    // plus that ACE's four bays (global 4-7). Seven spools, eight slots.
    REQUIRE(backend.owned_spool_slots() == std::vector<int>{0, 1, 2, 4, 5, 6, 7});

    SECTION("the U1's own heads number from 1") {
        CHECK(backend.spool_display_number(0) == 1);
        CHECK(backend.spool_display_number(1) == 2);
        CHECK(backend.spool_display_number(2) == 3);
    }

    SECTION("the ACE's bays continue the run without a gap") {
        // Numbering by global index labelled these 5-8: the ACE starts at global
        // 4 because the U1 reserves 0-3, so the head/bay double-count pushed
        // every ACE bay up by one.
        CHECK(backend.spool_display_number(4) == 4);
        CHECK(backend.spool_display_number(5) == 5);
        CHECK(backend.spool_display_number(6) == 6);
        CHECK(backend.spool_display_number(7) == 7);
    }

    SECTION("an ACE-fed head shows the number of the bay feeding it") {
        // T3 and ACE bay 0 are one physical spool, so they read the same number
        // rather than taking one each.
        REQUIRE(backend.slot_identity_owner_slot(3) == 4);
        CHECK(backend.spool_display_number(3) == backend.spool_display_number(4));
        CHECK(backend.spool_display_number(3) == 4);
    }

    SECTION("an ACE-fed head is LABELLED with the range that can feed it") {
        // Head mode: the ACE binds to one head and ALL FOUR of its bays feed it.
        // Labelling the seated bay alone would be a number that moves under the
        // user on every swap, and would hide the other three.
        CHECK(backend.spool_display_label(3) == "4-7");

        // Slots that hold their own spool are still a bare number.
        CHECK(backend.spool_display_label(0) == "1");
        CHECK(backend.spool_display_label(2) == "3");
        CHECK(backend.spool_display_label(4) == "4");
        CHECK(backend.spool_display_label(7) == "7");
    }

    SECTION("a feeder head owns its spool") {
        CHECK_FALSE(backend.slot_identity_owner_slot(0).has_value());
        CHECK_FALSE(backend.slot_identity_owner_slot(2).has_value());
    }

    SECTION("an ACE bay is never owned by anything") {
        for (int bay = 4; bay <= 7; ++bay) {
            INFO("bay global index " << bay);
            CHECK_FALSE(backend.slot_identity_owner_slot(bay).has_value());
        }
    }

    SECTION("no number is used twice") {
        std::vector<int> seen;
        for (int s : backend.owned_spool_slots()) {
            seen.push_back(backend.spool_display_number(s));
        }
        std::sort(seen.begin(), seen.end());
        CHECK(seen == std::vector<int>{1, 2, 3, 4, 5, 6, 7});
    }
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE keeps an ACE-fed head bound while it is EMPTY",
                 "[ams][multiace]") {
    // The live rig with nothing loaded: head 3 is still wired to ACE 0
    // (head_feeder false, head_ace 3->0) but head_source has gone all-null
    // because the filament left. The wiring did not change; only the contents.
    //
    // Binding on head_source alone broke all of this at once on hardware: the
    // ACE's bays lost mapped_tool, so the overview's cross-unit head sharing
    // stopped firing and drew a FIFTH nozzle labelled T4; the U1 went back to
    // claiming four spool positions; and a line reappeared from SnapSwap to T3.
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(
        json{{"mode", "head"},
             {"device_count", 1},
             {"head_ace", {{"0", 0}, {"1", 1}, {"2", 2}, {"3", 0}}},
             {"head_feeder", {{"0", true}, {"1", true}, {"2", true}, {"3", false}}},
             {"head_manual", {{"0", false}, {"1", false}, {"2", false}, {"3", false}}},
             {"head_source", {{"0", nullptr}, {"1", nullptr}, {"2", nullptr}, {"3", nullptr}}},
             {"aces", json::array({json{{"idx", 0}, {"connected", true}, {"protocol", "v2"}}})}}));

    REQUIRE(backend.head_source_kind(3) == HeadSource::ACE);
    CHECK_FALSE(backend.seated_source(3).has_value()); // genuinely empty

    SECTION("the head still belongs to the ACE") {
        CHECK(backend.slot_identity_owner_unit(3) == 1);
    }

    SECTION("the U1 still owns only three spool positions") {
        CHECK(backend.owned_spool_slots() == std::vector<int>{0, 1, 2, 4, 5, 6, 7});
    }

    SECTION("every ACE bay still points at the head it feeds") {
        // This is what the overview's head sharing keys on. All four bays at -1
        // is what let the ACE allocate a nozzle of its own.
        const auto info = backend.get_system_info();
        REQUIRE(info.units.size() == 2);
        for (int s = 0; s < 4; ++s) {
            INFO("bay " << s);
            CHECK(info.units[1].slots[static_cast<size_t>(s)].mapped_tool == 3);
        }
    }

    SECTION("the head still labels as the range that can feed it") {
        CHECK(backend.spool_display_label(3) == "4-7");
    }
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE multi mode labels a head with one bay, not a range",
                 "[ams][multiace]") {
    // In multi mode bay s feeds head s, so exactly one bay can reach each head
    // and the range collapses to a single number with no special-casing. This is
    // the shape Gordian's machine is NOT in, which is why it needs pinning.
    CapturingMultiAce backend;
    json ace = json{{"mode", "multi"},
                    {"device_count", 1},
                    {"head_feeder", {{"0", false}, {"1", false}, {"2", false}, {"3", false}}},
                    {"head_source",
                     {{"0", json{{"ace_index", 0}, {"slot", 0}}},
                      {"1", json{{"ace_index", 0}, {"slot", 1}}},
                      {"2", json{{"ace_index", 0}, {"slot", 2}}},
                      {"3", json{{"ace_index", 0}, {"slot", 3}}}}},
                    {"aces", json::array({json{{"idx", 0}, {"connected", true}}})}};
    backend.handle_status_update(wrap(ace));

    // Every U1 head is fed from the ACE here, so the only spools are its bays.
    REQUIRE(backend.owned_spool_slots() == std::vector<int>{4, 5, 6, 7});

    CHECK(backend.spool_display_label(4) == "1");
    CHECK(backend.spool_display_label(7) == "4");
    // Head 0 is reachable from bay 0 alone — a number, not "1-4".
    CHECK(backend.spool_display_label(0) == "1");
    CHECK(backend.spool_display_label(3) == "4");
}

// ============================================================================
// Auto-dry — humidity-controlled drying
//
// ACE_SET_AUTO_DRY's parameter names are stated nowhere in multiACE's bundled
// docs; they were read off its own web UI (see the plan's § Auto-dry). The
// captured fixture carries the whole block, so these run against the real shape.
// ============================================================================

TEST_CASE_METHOD(HelixTestFixture, "multiACE reads auto-dry off the live frame",
                 "[ams][multiace][autodry]") {
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(live_ace_object()));

    // Unit 1 is ACE 0 — unit 0 is the U1 itself.
    const AutoDryInfo info = backend.get_auto_dry_info(1);
    REQUIRE(info.supported);
    CHECK_FALSE(info.enabled);
    CHECK_FALSE(info.running);
    CHECK(info.rh_start_pct == Catch::Approx(45.0f));
    CHECK(info.rh_end_pct == Catch::Approx(35.0f));
    CHECK(info.temp_c == 50);
    // The captured unit is an ACE 2 Pro (protocol v2): it has its own humidity
    // sensor, so it evaluates the threshold itself rather than following anyone.
    CHECK_FALSE(info.follows_master);
    CHECK(info.can_enable());
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE arms auto-dry without restating thresholds",
                 "[ams][multiace][autodry]") {
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(live_ace_object()));

    SECTION("on") {
        auto err = backend.set_auto_dry_enabled(true, 1);
        CHECK(err.success());
        REQUIRE(backend.captured_gcodes.size() == 1);
        // ENABLE alone. Every field of ACE_SET_AUTO_DRY is independent and
        // persisted, so restating a threshold here would silently overwrite one
        // the user had set in multiACE's own UI.
        CHECK(backend.captured_gcodes[0] == "ACE_SET_AUTO_DRY ACE=0 ENABLE=1");
    }
    SECTION("off") {
        auto err = backend.set_auto_dry_enabled(false, 1);
        CHECK(err.success());
        REQUIRE(backend.captured_gcodes.size() == 1);
        CHECK(backend.captured_gcodes[0] == "ACE_SET_AUTO_DRY ACE=0 ENABLE=0");
    }
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE keeps auto-dry across a partial frame",
                 "[ams][multiace][autodry]") {
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(live_ace_object()));
    REQUIRE(backend.get_auto_dry_info(1).rh_start_pct == Catch::Approx(45.0f));

    // Moonraker sends DELTAS: a frame that does not mention auto_dry says
    // nothing about it, and must not be read as "the rule is gone".
    backend.handle_status_update(
        wrap(json{{"aces", json::array({json{{"idx", 0}, {"temp", 31}}})}}));

    const AutoDryInfo info = backend.get_auto_dry_info(1);
    CHECK(info.supported);
    CHECK(info.rh_start_pct == Catch::Approx(45.0f));
    CHECK(info.rh_end_pct == Catch::Approx(35.0f));
    CHECK(info.temp_c == 50);
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE offers no auto-dry on the U1 itself",
                 "[ams][multiace][autodry]") {
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(live_ace_object()));

    // Unit 0 is the four SnapSwap heads. There is no chamber there to dry.
    CHECK_FALSE(backend.get_auto_dry_info(0).supported);
    auto err = backend.set_auto_dry_enabled(true, 0);
    CHECK_FALSE(err.success());
    CHECK(backend.captured_gcodes.empty());
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE reports no auto-dry when the frame omits the block",
                 "[ams][multiace][autodry]") {
    // Firmware predating auto-dry sends the unit with no `auto_dry` at all.
    // That is not "disabled" — there is no rule to show a switch for.
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(
        json{{"device_count", 1},
             {"mode", "head"},
             {"aces", json::array({json{{"idx", 0}, {"connected", true}, {"protocol", "v2"}}})}}));

    CHECK_FALSE(backend.get_auto_dry_info(1).supported);
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE refuses to arm a follower with no master",
                 "[ams][multiace][autodry]") {
    // A v1 ACE Pro has no humidity sensor, so it cannot evaluate a threshold and
    // instead mirrors a v2 unit's cycle. With no master picked the firmware
    // rejects the arm, so we refuse before sending rather than after.
    auto frame = [](int master) {
        return wrap(json{{"device_count", 1},
                         {"mode", "head"},
                         {"aces", json::array({json{{"idx", 0},
                                                    {"connected", true},
                                                    {"protocol", "v1"},
                                                    {"auto_dry", json{{"enabled", false},
                                                                      {"rh_start", 45.0},
                                                                      {"rh_end", 35.0},
                                                                      {"temp", 50},
                                                                      {"master", master},
                                                                      {"add_time", 60}}}}})}});
    };

    SECTION("no master picked") {
        CapturingMultiAce backend;
        backend.handle_status_update(frame(-1));

        const AutoDryInfo info = backend.get_auto_dry_info(1);
        REQUIRE(info.supported);
        CHECK(info.follows_master);
        CHECK(info.master_unit == -1);
        CHECK_FALSE(info.can_enable());

        auto err = backend.set_auto_dry_enabled(true, 1);
        CHECK_FALSE(err.success());
        CHECK(backend.captured_gcodes.empty());

        // Disarming stays available — it is always a safe direction to move.
        CHECK(backend.set_auto_dry_enabled(false, 1).success());
        CHECK(backend.captured_gcodes.size() == 1);
    }
    SECTION("master picked") {
        CapturingMultiAce backend;
        backend.handle_status_update(frame(0));

        const AutoDryInfo info = backend.get_auto_dry_info(1);
        CHECK(info.follows_master);
        // multiACE names the master by ACE index; AMS units are one higher
        // because unit 0 is the U1.
        CHECK(info.master_unit == 1);
        CHECK(info.can_enable());

        CHECK(backend.set_auto_dry_enabled(true, 1).success());
        REQUIRE(backend.captured_gcodes.size() == 1);
        CHECK(backend.captured_gcodes[0] == "ACE_SET_AUTO_DRY ACE=0 ENABLE=1");
    }
}

TEST_CASE_METHOD(HelixTestFixture, "multiACE tracks whether auto-dry is the one drying",
                 "[ams][multiace][autodry]") {
    CapturingMultiAce backend;
    backend.handle_status_update(wrap(live_ace_object()));
    REQUIRE_FALSE(backend.get_auto_dry_info(1).running);

    // `auto_dry_running` is a sibling of the block, not a field inside it, and
    // arrives on its own — so it has to be parsed outside the `auto_dry` guard
    // or a frame carrying only this key would be dropped.
    backend.handle_status_update(
        wrap(json{{"aces", json::array({json{{"idx", 0}, {"auto_dry_running", true}}})}}));

    const AutoDryInfo info = backend.get_auto_dry_info(1);
    CHECK(info.running);
    // ...and the rest of the block survived the partial frame.
    CHECK(info.rh_start_pct == Catch::Approx(45.0f));
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

TEST_CASE_METHOD(HelixTestFixture, "multiACE reports environment sensors", "[ams][multiace]") {
    // Inherited from AmsBackendSnapmaker this answered false, and the unit
    // detail page uses it as a hard gate: ams_detail_pre_show_env_indicator()
    // adds LV_OBJ_FLAG_HIDDEN outright when the backend says no. That hid the
    // temperature/humidity badge on a drilled-into ACE, and with it the only
    // route to that unit's dryer and auto-dry controls.
    //
    // The answer is a backend-wide capability, as the interface asks it. Unit 0
    // is the U1 itself and has no sensor; the per-unit ams_env_ind_<n>_visible
    // subject is what hides the badge there.
    CapturingMultiAce backend;
    CHECK(backend.has_environment_sensors());

    // True before any frame too — the gate runs on panel show, which can beat
    // the first `ace` update, and a false there would hide the badge for good.
    AmsBackendSnapmaker plain(nullptr, nullptr);
    CHECK_FALSE(plain.has_environment_sensors()); // a stock U1 still has none
}
