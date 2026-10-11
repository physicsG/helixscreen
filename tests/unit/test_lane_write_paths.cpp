// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The write paths that reach a lane's records without going through a
// backend's parse loop: the consumption meter, and a user clearing a slot.
//
// Both predate the lane source model and both used to write only the stores
// that existed when they were written. A store with several writers is only as
// good as its least-updated one, so what is pinned here is that these two now
// reach it.

#include "../lvgl_test_fixture.h"
#include "ams_backend_afc.h"
#include "ams_backend_mock.h"
#include "ams_state.h"
#include "ams_types.h"
#include "lane_apply.h"
#include "lane_observation.h"
#include "lane_resolver.h"
#include "lane_source_store.h"
#include "lane_translation.h"
#include "test_helpers/registered_backend.h"
#include "test_helpers/seeded_override.h"

#include "../catch_amalgamated.hpp"

using helix::AmsBackendAfc;
using helix::AmsBackendMock;
using helix::SlotInfo;
using helix::SlotStatus;
using helix::ams::apply_resolved;
using helix::ams::commit_slot_edit;
using helix::ams::ingest;
using helix::ams::lane_sources;
using helix::ams::Observation;
using helix::ams::ObservationSource;
using helix::ams::resolve;
using helix::test::RegisteredBackend;

namespace {

/// A lane carrying a person's declaration and a server's, the two records a
/// clear has to take with it.
void declare_identity(helix::ams::LaneId lane) {
    Observation server(ObservationSource::Spoolman);
    server.spoolman_id = 42;
    server.brand = "Polymaker";
    ingest(lane, server);

    Observation user(ObservationSource::LocalUser);
    user.spool_name = "Bench reel";
    commit_slot_edit(lane, user);

    Observation meter(ObservationSource::Metered);
    meter.remaining_weight_g = 480.0F;
    ingest(lane, meter);
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "Clearing a slot stops its identity painting the lane",
                 "[lane][writepath]") {
    // The clear reaches two stores. Erasing the backend's override while the
    // lane's records stand leaves resolve() still reporting what the user just
    // removed, and the lane repaints it on the next frame.
    RegisteredBackend<AmsBackendAfc> harness(nullptr, nullptr);
    const auto lane = harness.lane(0);

    declare_identity(lane);

    // What the lane shows before the clear, laid over a slot as a backend
    // would have built it.
    SlotInfo before;
    before.brand = "unset";
    before.spool_name = "unset";
    apply_resolved(before, resolve(lane_sources(lane)));
    REQUIRE(before.brand == "Polymaker");
    REQUIRE(before.spool_name == "Bench reel");
    REQUIRE(before.spoolman_id == 42);

    harness->clear_slot_override(0);

    const auto after_sources = lane_sources(lane);
    CHECK_FALSE(after_sources.spoolman.has_value());
    CHECK_FALSE(after_sources.local_user.has_value());
    CHECK_FALSE(after_sources.metered.has_value());

    // The lane now states none of it, so a backend's own values stand where
    // the cleared identity used to paint.
    SlotInfo after;
    after.brand = "from firmware";
    after.spool_name = "from firmware";
    after.spoolman_id = 0;
    apply_resolved(after, resolve(lane_sources(lane)));
    CHECK(after.brand == "from firmware");
    CHECK(after.spool_name == "from firmware");
    CHECK(after.spoolman_id == 0);
}

TEST_CASE_METHOD(LVGLTestFixture, "A clear leaves the machine's own readings alone",
                 "[lane][writepath]") {
    // A clear is a statement about the record somebody made, not about what
    // the hardware reports. Dropping the sensor's answer would empty a loaded
    // lane, and dropping the vendor cache would throw away firmware's own
    // colour and material, which is what should paint once the record is gone.
    RegisteredBackend<AmsBackendAfc> harness(nullptr, nullptr);
    const auto lane = harness.lane(0);

    declare_identity(lane);

    Observation sensed(ObservationSource::Sensed);
    sensed.present = true;
    ingest(lane, sensed);

    Observation cache(ObservationSource::VendorCache);
    cache.material = "PETG";
    cache.color_rgb = 0xED2C2C;
    ingest(lane, cache);

    harness->clear_slot_override(0);

    const auto after = lane_sources(lane);
    REQUIRE(after.sensed.has_value());
    CHECK(after.sensed->present == true);
    REQUIRE(after.vendor_cache.has_value());
    CHECK(after.vendor_cache->material == "PETG");

    const auto r = resolve(after);
    CHECK(r.present == true);
    CHECK(r.material == "PETG");
    CHECK(r.color_rgb == 0xED2C2Cu);
}

TEST_CASE_METHOD(LVGLTestFixture, "A cleared field hands the lane back to the machine at once",
                 "[lane][writepath][1661]") {
    // A clear withdraws the user's word rather than stating an emptiness: the
    // moment it commits, nothing may sit over the field on the user rung, so
    // the machine's own reading paints the lane again with no restart
    // (prestonbrown/helixscreen#1661). The stored record carries the clear
    // through the edit's values whatever the rung does.
    RegisteredBackend<AmsBackendAfc> harness(nullptr, nullptr);
    const auto lane = harness.lane(0);

    Observation cache(ObservationSource::VendorCache);
    cache.brand = "Firmware Brand";
    cache.material = "PETG";
    cache.spool_name = "Firmware Reel";
    cache.spoolman_vendor_id = 9;
    ingest(lane, cache);

    // One edit that types a value (so a declaration stands to be withdrawn),
    // then one that clears it. The editor opens on what the lane resolves, the
    // same before the production editor sees.
    const auto commit = [](helix::ams::LaneId l, const SlotInfo& original, const SlotInfo& edited) {
        commit_slot_edit(l, helix::ams::user_edit_observation(original, edited));
    };
    const auto lane_shows = [lane] { return resolve(lane_sources(lane)); };

    SECTION("brand") {
        SlotInfo open;
        apply_resolved(open, lane_shows());
        REQUIRE(open.brand == "Firmware Brand");
        SlotInfo typed = open;
        typed.brand = "My Brand";
        commit(lane, open, typed);
        REQUIRE(lane_shows().brand.value_or("") == "My Brand");
        SlotInfo cleared = typed;
        cleared.brand.clear();
        commit(lane, typed, cleared);
        CHECK(lane_shows().brand.value_or("") == "Firmware Brand");
    }

    SECTION("material") {
        SlotInfo open;
        apply_resolved(open, lane_shows());
        SlotInfo typed = open;
        typed.material = "ABS";
        commit(lane, open, typed);
        REQUIRE(lane_shows().material.value_or("") == "ABS");
        SlotInfo cleared = typed;
        cleared.material.clear();
        commit(lane, typed, cleared);
        CHECK(lane_shows().material.value_or("") == "PETG");
    }

    SECTION("spool name") {
        SlotInfo open;
        apply_resolved(open, lane_shows());
        SlotInfo typed = open;
        typed.spool_name = "Bench reel";
        commit(lane, open, typed);
        REQUIRE(lane_shows().spool_name.value_or("") == "Bench reel");
        SlotInfo cleared = typed;
        cleared.spool_name.clear();
        commit(lane, typed, cleared);
        CHECK(lane_shows().spool_name.value_or("") == "Firmware Reel");
    }

    SECTION("vendor id") {
        SlotInfo open;
        apply_resolved(open, lane_shows());
        SlotInfo typed = open;
        typed.spoolman_vendor_id = 4;
        commit(lane, open, typed);
        REQUIRE(lane_shows().spoolman_vendor_id.value_or(0) == 4);
        SlotInfo cleared = typed;
        cleared.spoolman_vendor_id = 0;
        commit(lane, typed, cleared);
        CHECK(lane_shows().spoolman_vendor_id.value_or(0) == 9);
    }

    SECTION("a clear of one field leaves a sibling declaration standing") {
        SlotInfo open;
        apply_resolved(open, lane_shows());
        SlotInfo typed = open;
        typed.brand = "My Brand";
        typed.spool_name = "Bench reel";
        commit(lane, open, typed);
        SlotInfo cleared = typed;
        cleared.brand.clear();
        commit(lane, typed, cleared);
        CHECK(lane_shows().brand.value_or("") == "Firmware Brand");
        CHECK(lane_shows().spool_name.value_or("") == "Bench reel");
    }
}

TEST_CASE_METHOD(LVGLTestFixture, "A cleared field on a lane the machine is silent on stays blank",
                 "[lane][writepath][1661]") {
    // No vendor cache, no spool: the clear withdraws the declaration and
    // nothing states the field, so the lane shows blank rather than the value
    // the withdrawn declaration stood over (prestonbrown/helixscreen#1661).
    RegisteredBackend<AmsBackendAfc> harness(nullptr, nullptr);
    const auto lane = harness.lane(0);

    SlotInfo open;
    open.brand = "Whatever was loaded";
    open.material = "PLA";
    SlotInfo typed = open;
    typed.brand = "My Brand";
    typed.material = "ABS";
    commit_slot_edit(lane, helix::ams::user_edit_observation(open, typed));
    REQUIRE(resolve(lane_sources(lane)).brand.value_or("") == "My Brand");

    SlotInfo cleared = typed;
    cleared.brand.clear();
    cleared.material.clear();
    commit_slot_edit(lane, helix::ams::user_edit_observation(typed, cleared));
    const auto after = resolve(lane_sources(lane));
    CHECK_FALSE(after.brand.has_value());
    CHECK_FALSE(after.material.has_value());
}

TEST_CASE_METHOD(LVGLTestFixture, "A metered weight reaches the lane it was measured on",
                 "[lane][writepath]") {
    // update_slot_weight is the consumption tracker's and Spoolman's way in,
    // and neither is a backend. Without this the number a lane shows and the
    // number the meter holds are two stores that drift apart for a whole print.
    RegisteredBackend<AmsBackendMock> harness(4);

    harness->update_slot_weight(1, 400.0F, 900.0F, /*persist=*/false);

    const auto sources = lane_sources(harness.lane(1));
    REQUIRE(sources.metered.has_value());
    CHECK(sources.metered->remaining_weight_g == Catch::Approx(400.0F));
    CHECK(sources.metered->total_weight_g == Catch::Approx(900.0F));

    // On the lane it was measured on, and no other.
    CHECK_FALSE(lane_sources(harness.lane(0)).metered.has_value());
}

TEST_CASE_METHOD(LVGLTestFixture, "A weight update with no total leaves the backend's total alone",
                 "[lane][writepath]") {
    // A total below zero is the caller saying it has none to report. Filing it
    // as a reading would state a weight nobody measured; filing nothing leaves
    // the backend's own total standing, which is the "leave unchanged" the
    // parameter asks for.
    RegisteredBackend<AmsBackendMock> harness(4);

    harness->update_slot_weight(2, 250.0F, -1.0F, /*persist=*/false);

    const auto sources = lane_sources(harness.lane(2));
    REQUIRE(sources.metered.has_value());
    CHECK(sources.metered->remaining_weight_g == Catch::Approx(250.0F));
    CHECK_FALSE(sources.metered->total_weight_g.has_value());

    SlotInfo slot;
    slot.total_weight_g = 1000.0F;
    apply_resolved(slot, resolve(sources));
    CHECK(slot.remaining_weight_g == Catch::Approx(250.0F));
    CHECK(slot.total_weight_g == Catch::Approx(1000.0F));
}

TEST_CASE_METHOD(LVGLTestFixture, "A firmware frame cannot erase what a stored record remembered",
                 "[lane][writepath][remembered]") {
    // The defect Remembered exists to close. A stored record and a backend's
    // own parse both used to file VendorCache, ingest() replaces a source's
    // record whole, and a frame states only what that frame knows. So a brand
    // nobody had retyped since the last boot disappeared on the first poll.
    RegisteredBackend<AmsBackendAfc> harness(nullptr, nullptr);
    const auto lane = harness.lane(0);

    helix::ams::FilamentSlotOverride stored;
    stored.brand = "Polymaker";
    stored.material = "ASA";
    helix::test::file_override_as_lane_records(*harness, 0, stored);

    REQUIRE(lane_sources(lane).remembered.has_value());
    REQUIRE(lane_sources(lane).remembered->brand == "Polymaker");

    // The machine speaks, and says nothing about a brand.
    Observation frame(ObservationSource::VendorCache);
    frame.material = "PLA";
    frame.color_rgb = 0xED2C2Cu;
    ingest(lane, frame);

    const auto after = lane_sources(lane);
    REQUIRE(after.remembered.has_value());
    CHECK(after.remembered->brand == "Polymaker");

    const auto r = resolve(after);
    // The frame outranks the disk copy on what it actually stated, and leaves
    // the rest of the disk copy standing.
    CHECK(r.material == "PLA");
    CHECK(r.color_rgb == 0xED2C2Cu);
    CHECK(r.brand == "Polymaker");
}

TEST_CASE_METHOD(LVGLTestFixture, "A remembered field never outranks the machine stating it",
                 "[lane][writepath][remembered]") {
    // Remembered is the weakest rung. A printer that does state the field wins,
    // or last boot's copy would pin a value the machine has moved on from.
    RegisteredBackend<AmsBackendAfc> harness(nullptr, nullptr);
    const auto lane = harness.lane(0);

    helix::ams::FilamentSlotOverride stored;
    stored.brand = "Polymaker";
    helix::test::file_override_as_lane_records(*harness, 0, stored);

    Observation frame(ObservationSource::VendorCache);
    frame.brand = "Elegoo";
    ingest(lane, frame);

    CHECK(resolve(lane_sources(lane)).brand == "Elegoo");
}
