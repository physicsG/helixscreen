// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "filament_slot_override.h"
#include "filament_slot_override_store.h"
#include "helix_test_fixture.h"
#include "lane_resolver.h"
#include "lane_source_store.h"
#include "lane_translation.h"
#include "test_helpers/log_capture.h"

#include <spdlog/spdlog.h>

#include "../catch_amalgamated.hpp"

using helix::ams::classify_declaration;
using helix::ams::declared_from_record;
using helix::ams::ingest;
using helix::ams::lane_id_value;
using helix::ams::lane_sources;
using helix::ams::LaneId;
using helix::ams::LegacyLockKeys;
using helix::ams::Observation;
using helix::ams::ObservationSource;
using helix::ams::sources_from_record;

// The store treats a lane id as an opaque key, so these cases name lanes by
// raw value. Everything that produces an id goes through lane_id_for().
TEST_CASE_METHOD(HelixTestFixture, "ingest writes one source and leaves the rest alone",
                 "[lane][ingest]") {
    Observation sensed(ObservationSource::Sensed);
    sensed.present = true;
    ingest(LaneId{2}, sensed);

    Observation cache(ObservationSource::VendorCache);
    cache.color_rgb = 0xED2C2C;
    cache.material = "PETG";
    ingest(LaneId{2}, cache);

    const auto lane = lane_sources(LaneId{2});
    REQUIRE(lane.sensed.has_value());
    CHECK(lane.sensed->present == true);
    REQUIRE(lane.vendor_cache.has_value());
    CHECK(lane.vendor_cache->color_rgb == 0xED2C2C);
    CHECK_FALSE(lane.spoolman.has_value());
    CHECK_FALSE(lane.local_user.has_value());
    CHECK_FALSE(lane.metered.has_value());
}

TEST_CASE_METHOD(HelixTestFixture, "ingest replaces a source's record whole", "[lane][ingest]") {
    Observation first(ObservationSource::VendorCache);
    first.color_rgb = 0xED2C2C;
    first.material = "PETG";
    ingest(LaneId{0}, first);

    // The vendor store stops reporting a material. Whole-record replacement is
    // what makes that stop contributing: a field the source has stopped
    // observing must not keep standing from an earlier frame.
    Observation second(ObservationSource::VendorCache);
    second.color_rgb = 0xED2C2C;
    ingest(LaneId{0}, second);

    const auto lane = lane_sources(LaneId{0});
    REQUIRE(lane.vendor_cache.has_value());
    CHECK(lane.vendor_cache->color_rgb == 0xED2C2C);
    CHECK_FALSE(lane.vendor_cache->material.has_value());
}

TEST_CASE_METHOD(HelixTestFixture, "lanes are independent destinations", "[lane][ingest]") {
    Observation a(ObservationSource::Sensed);
    a.present = true;
    ingest(LaneId{0}, a);

    Observation b(ObservationSource::Sensed);
    b.present = false;
    ingest(LaneId{1}, b);

    const auto lane0 = lane_sources(LaneId{0});
    const auto lane1 = lane_sources(LaneId{1});
    REQUIRE(lane0.sensed.has_value());
    REQUIRE(lane1.sensed.has_value());
    CHECK(lane0.sensed->present == true);
    CHECK(lane1.sensed->present == false);
}

TEST_CASE_METHOD(HelixTestFixture, "an unseen lane reads as nothing observed", "[lane][ingest]") {
    const auto lane = lane_sources(LaneId{7});
    CHECK_FALSE(lane.sensed.has_value());
    CHECK_FALSE(lane.vendor_cache.has_value());
    CHECK(helix::ams::known_lanes().empty());
}

TEST_CASE("a lane id names one backend's slot and nothing else", "[lane][ingest]") {
    using helix::ams::lane_id_for;
    using helix::ams::LANES_PER_BACKEND;

    // Two backends is the ordinary case, not an exotic one: a tool changer
    // beside a filament system is what makes a bare slot index wrong.
    CHECK(lane_id_for(0, 0) == LaneId{0});
    CHECK(lane_id_for(0, 3) == LaneId{3});
    CHECK(lane_id_for(1, 0) != lane_id_for(0, 0));
    CHECK(lane_id_for(1, 0) == LaneId{LANES_PER_BACKEND});

    // The last slot of one block never collides with the first of the next.
    CHECK(lane_id_for(0, LANES_PER_BACKEND - 1) < lane_id_for(1, 0));

    // The printer-level ids sit clear of every backend block.
    CHECK(helix::ams::FIRST_TOOL_LANE_ID > helix::ams::BYPASS_LANE_ID);

    // The last backend and slot this scheme supports still sits below the
    // bypass id, pinning the boundary MAX_BACKENDS exists to hold.
    CHECK(lane_id_for(helix::ams::MAX_BACKENDS - 1, LANES_PER_BACKEND - 1) <
          helix::ams::BYPASS_LANE_ID);
}

TEST_CASE("a backend block is sized for hardware, not for the subject array", "[lane][ingest]") {
    using helix::ams::lane_id_for;

    // AFC reports one lane per unit it finds and Happy Hare one per gate, both
    // uncapped by AmsState::MAX_SLOTS, which bounds only how many slots get
    // subjects. A five-unit BoxTurtle and a twenty-gate MMU are the shipped
    // hardware that exceeds it, and both must still address a lane of their own.
    CHECK(helix::ams::LANES_PER_BACKEND > 20);
    CHECK(lane_id_for(0, 20) != helix::ams::INVALID_LANE_ID);
    CHECK(lane_id_for(0, 20) != lane_id_for(1, 0));
}

TEST_CASE("a pair that names no lane yields no id", "[lane][ingest]") {
    using helix::ams::INVALID_LANE_ID;
    using helix::ams::lane_id_for;

    // A backend registration never reached leaves its index at -1, and a slot
    // index past the block is the neighbouring backend's slot. Neither may
    // resolve to an id: the nearest one is a real lane on a real backend, so
    // nothing downstream could tell the record apart from a deliberate write.
    CHECK(lane_id_for(-1, 0) == INVALID_LANE_ID);
    CHECK(lane_id_for(helix::ams::MAX_BACKENDS, 0) == INVALID_LANE_ID);
    CHECK(lane_id_for(0, -1) == INVALID_LANE_ID);
    CHECK(lane_id_for(0, helix::ams::LANES_PER_BACKEND) == INVALID_LANE_ID);

    CHECK_FALSE(helix::ams::is_lane_id(INVALID_LANE_ID));
    CHECK(helix::ams::is_lane_id(LaneId{0}));
}

TEST_CASE("only an id the scheme assigns is a lane", "[lane][ingest]") {
    using helix::ams::BYPASS_LANE_ID;
    using helix::ams::END_LANE_ID;
    using helix::ams::FIRST_TOOL_LANE_ID;
    using helix::ams::is_lane_id;
    using helix::ams::LANES_PER_BACKEND;
    using helix::ams::MAX_BACKENDS;

    constexpr int END_OF_BLOCKS = MAX_BACKENDS * LANES_PER_BACKEND;
    const auto plus = [](LaneId lane, int n) { return LaneId{lane_id_value(lane) + n}; };

    // Both ends of each of the three ranges the scheme assigns.
    CHECK(is_lane_id(LaneId{0}));
    CHECK(is_lane_id(LaneId{END_OF_BLOCKS - 1}));
    CHECK(is_lane_id(BYPASS_LANE_ID));
    CHECK(is_lane_id(FIRST_TOOL_LANE_ID));
    CHECK(is_lane_id(plus(END_LANE_ID, -1)));

    // One past each end, both ends of the gap between the last backend block
    // and the bypass, and an arbitrary large integer. A positive value is not
    // a lane merely for being positive: no backend, bypass or tool owns any
    // of these, so a record filed on one would describe nothing at all.
    CHECK_FALSE(is_lane_id(LaneId{-1}));
    CHECK_FALSE(is_lane_id(helix::ams::INVALID_LANE_ID));
    CHECK_FALSE(is_lane_id(LaneId{END_OF_BLOCKS}));
    CHECK_FALSE(is_lane_id(plus(BYPASS_LANE_ID, -1)));
    CHECK_FALSE(is_lane_id(plus(BYPASS_LANE_ID, 1)));
    CHECK_FALSE(is_lane_id(plus(FIRST_TOOL_LANE_ID, -1)));
    CHECK_FALSE(is_lane_id(END_LANE_ID));
    CHECK_FALSE(is_lane_id(LaneId{1000000}));

    // Everything lane_id_for produces is an id this admits, at the far corner.
    CHECK(is_lane_id(helix::ams::lane_id_for(MAX_BACKENDS - 1, LANES_PER_BACKEND - 1)));
}

TEST_CASE_METHOD(HelixTestFixture, "the store cannot grow past the ids the scheme assigns",
                 "[lane][ingest]") {
    Observation obs(ObservationSource::Sensed);
    obs.present = true;

    // Each rejected id logs, and this offers a great many of them.
    const auto restore_level = spdlog::default_logger()->level();
    spdlog::set_level(spdlog::level::critical);

    // Ids no backend, bypass or tool can own. "Bounded by construction" is
    // only true if the funnels refuse these: a backend deriving an id wrongly
    // in a later plan would otherwise grow the map for as long as it polls.
    const int end = lane_id_value(helix::ams::END_LANE_ID);
    for (int lane = end; lane < end + 200000; ++lane) {
        ingest(LaneId{lane}, obs);
    }

    // The gap between the last backend block and the bypass is the same
    // question in the range a miscomputed backend id would land in.
    for (int lane = helix::ams::MAX_BACKENDS * helix::ams::LANES_PER_BACKEND;
         lane < lane_id_value(helix::ams::BYPASS_LANE_ID); ++lane) {
        ingest(LaneId{lane}, obs);
    }

    spdlog::set_level(restore_level);

    CHECK(helix::ams::known_lanes().empty());

    // The three ranges that are lanes still write, so the refusal above is
    // selective rather than a funnel that stopped working.
    ingest(LaneId{0}, obs);
    ingest(helix::ams::BYPASS_LANE_ID, obs);
    ingest(helix::ams::FIRST_TOOL_LANE_ID, obs);
    CHECK(helix::ams::known_lanes().size() == 3);
    CHECK(static_cast<int>(helix::ams::known_lanes().size()) <= helix::ams::MAX_LANES);
}

TEST_CASE_METHOD(HelixTestFixture, "a funnel handed no lane writes nothing", "[lane][ingest]") {
    Observation sensed(ObservationSource::Sensed);
    sensed.present = true;
    ingest(helix::ams::INVALID_LANE_ID, sensed);

    Observation user(ObservationSource::LocalUser);
    user.color_rgb = 0xBCBCBC;
    helix::ams::commit_slot_edit(helix::ams::INVALID_LANE_ID, user);

    // Not a clamp onto lane 0, and not a record filed under the id itself.
    CHECK(helix::ams::known_lanes().empty());
    CHECK_FALSE(lane_sources(LaneId{0}).sensed.has_value());
    CHECK_FALSE(lane_sources(LaneId{0}).local_user.has_value());
    CHECK_FALSE(lane_sources(helix::ams::INVALID_LANE_ID).sensed.has_value());
}

TEST_CASE_METHOD(HelixTestFixture, "a dropped lane is reported once, and again when the id changes",
                 "[lane][ingest]") {
    helix::LogCapture log;

    Observation sensed(ObservationSource::Sensed);
    sensed.present = true;

    // A producer filing through a backend that has no index yet reaches this
    // three times per lane per frame, and the id is the whole content of the
    // message, so the second and third repeat tell a reader nothing.
    ingest(helix::ams::INVALID_LANE_ID, sensed);
    ingest(helix::ams::INVALID_LANE_ID, sensed);
    ingest(helix::ams::INVALID_LANE_ID, sensed);
    CHECK(log.count_containing("names no position") == 1);

    // The other funnel, on the SAME id: a machine reading filed on no lane and
    // a person's edit thrown away are not the same loss, and the producer's
    // flood runs first at startup, so one latch for both would silence the
    // half that matters more.
    Observation user(ObservationSource::LocalUser);
    user.color_rgb = 0xBCBCBC;
    helix::ams::commit_slot_edit(helix::ams::INVALID_LANE_ID, user);
    CHECK(log.count_containing("names no position") == 2);

    // It latches the same way once it has spoken.
    helix::ams::commit_slot_edit(helix::ams::INVALID_LANE_ID, user);
    CHECK(log.count_containing("names no position") == 2);

    // A different id is a different fact and speaks for itself.
    ingest(helix::ams::END_LANE_ID, sensed);
    CHECK(log.count_containing("names no position") == 3);
}

TEST_CASE("a lane colour string reads as a value, a clear or nothing", "[lane][ingest]") {
    using helix::ams::ColorReadingKind;
    using helix::ams::read_lane_color;

    SECTION("a colour, however the producer spells it") {
        CHECK(read_lane_color("#ED2C2C").kind == ColorReadingKind::Observed);
        CHECK(read_lane_color("#ED2C2C").rgb == 0xED2C2Cu);
        CHECK(read_lane_color("ED2C2C").rgb == 0xED2C2Cu);
        CHECK(read_lane_color("0xED2C2C").rgb == 0xED2C2Cu);
        CHECK(read_lane_color("ed2c2c").rgb == 0xED2C2Cu);
        // Pure black is a colour a spool can be, not a failure.
        CHECK(read_lane_color("#000000").kind == ColorReadingKind::Observed);
        CHECK(read_lane_color("#000000").rgb == 0x000000u);
    }

    SECTION("the short form expands rather than reading as a near-black") {
        CHECK(read_lane_color("#F00").kind == ColorReadingKind::Observed);
        CHECK(read_lane_color("#F00").rgb == 0xFF0000u);
    }

    SECTION("a slicer's 8-digit form drops alpha rather than carrying it") {
        CHECK(read_lane_color("#800080FF").kind == ColorReadingKind::Observed);
        CHECK(read_lane_color("#800080FF").rgb == 0x800080u);
    }

    SECTION("nothing but a prefix is the producer clearing the lane") {
        CHECK(read_lane_color("").kind == ColorReadingKind::Cleared);
        CHECK(read_lane_color("#").kind == ColorReadingKind::Cleared);
        CHECK(read_lane_color("  ").kind == ColorReadingKind::Cleared);
        CHECK(read_lane_color(" # ").kind == ColorReadingKind::Cleared);
    }

    SECTION("a value that is not a colour is no reading, which is not a clear") {
        // Each of these has a reading a bare std::stoul would hand back: a
        // partial parse of the head, or a negation. None of them is what the
        // producer meant.
        CHECK(read_lane_color("#zzzzzz").kind == ColorReadingKind::NoReading);
        CHECK(read_lane_color("FF0000junk").kind == ColorReadingKind::NoReading);
        CHECK(read_lane_color("-1").kind == ColorReadingKind::NoReading);
        CHECK(read_lane_color("beef").kind == ColorReadingKind::NoReading);
        CHECK(read_lane_color("None").kind == ColorReadingKind::NoReading);
    }
}

TEST_CASE("the blocks are adjacent, which is why a slot index is bounded", "[lane][ingest]") {
    using helix::ams::lane_id_for;
    using helix::ams::LANES_PER_BACKEND;

    // No gap between one block's last id and the next block's first. That is
    // what makes lane_id_for's slot_index bound load-bearing rather than
    // defensive: a slot index one past a block is not an unused id, it is the
    // neighbouring backend's slot 0, and every index past that is one of its
    // real slots.
    CHECK(lane_id_value(lane_id_for(0, LANES_PER_BACKEND - 1)) + 1 ==
          lane_id_value(lane_id_for(1, 0)));
    CHECK(lane_id_value(lane_id_for(3, LANES_PER_BACKEND - 1)) + 1 ==
          lane_id_value(lane_id_for(4, 0)));

    // The id a bounds violation would have produced belongs to a real slot on
    // a real backend, so nothing downstream could tell it apart.
    CHECK(lane_id_value(lane_id_for(0, 0)) + (6 * LANES_PER_BACKEND + 3) ==
          lane_id_value(lane_id_for(6, 3)));
}

TEST_CASE_METHOD(HelixTestFixture, "commit_slot_edit refuses a source that is not the user",
                 "[lane][ingest]") {
    Observation cache(ObservationSource::VendorCache);
    cache.color_rgb = 0xED2C2C;
    helix::ams::commit_slot_edit(LaneId{5}, cache);

    // The two funnels take the same arguments and mean opposite things, so the
    // source check is what stops a backend reaching for the amending one and
    // becoming a third writer of a record the user owns. It returns void, so a
    // caller cannot tell a drop from a write; the lane is where that shows.
    const auto lane = lane_sources(LaneId{5});
    CHECK_FALSE(lane.vendor_cache.has_value());
    CHECK_FALSE(lane.local_user.has_value());
    CHECK(helix::ams::known_lanes().empty());
}

TEST_CASE_METHOD(HelixTestFixture,
                 "ingest refuses a LocalUser observation and leaves the user's record alone",
                 "[lane][ingest]") {
    Observation declared(ObservationSource::LocalUser);
    declared.color_rgb = 0xBCBCBC;
    helix::ams::commit_slot_edit(LaneId{6}, declared);

    const auto before = lane_sources(LaneId{6});
    REQUIRE(before.local_user.has_value());
    CHECK(before.local_user->color_rgb == 0xBCBCBC);

    // ingest() replaces whole-record, so a LocalUser observation reaching it
    // would destroy the user's declaration rather than merely fail to amend
    // it. The colour differs from the one above so a silent pass-through
    // shows up as a changed value, not a coincidental match.
    Observation impostor(ObservationSource::LocalUser);
    impostor.color_rgb = 0x000000;
    ingest(LaneId{6}, impostor);

    const auto after = lane_sources(LaneId{6});
    REQUIRE(after.local_user.has_value());
    CHECK(after.local_user->color_rgb == 0xBCBCBC);
}

TEST_CASE_METHOD(HelixTestFixture, "known_lanes lists every lane that has been written",
                 "[lane][ingest]") {
    Observation obs(ObservationSource::Sensed);
    obs.present = true;
    ingest(LaneId{3}, obs);
    ingest(LaneId{0}, obs);

    const auto lanes = helix::ams::known_lanes();
    REQUIRE(lanes.size() == 2);
    CHECK(lanes[0] == LaneId{0});
    CHECK(lanes[1] == LaneId{3});
}

TEST_CASE("every ObservationSource round-trips to its own LaneSources member", "[lane]") {
    const ObservationSource sources[] = {
        ObservationSource::Sensed, ObservationSource::Spoolman, ObservationSource::LocalUser,
        ObservationSource::VendorCache, ObservationSource::Metered};

    for (ObservationSource s : sources) {
        helix::ams::LaneSources lane;
        Observation obs(s);
        obs.present = true;
        lane.apply(obs);

        CHECK(lane.sensed.has_value() == (s == ObservationSource::Sensed));
        CHECK(lane.spoolman.has_value() == (s == ObservationSource::Spoolman));
        CHECK(lane.local_user.has_value() == (s == ObservationSource::LocalUser));
        CHECK(lane.vendor_cache.has_value() == (s == ObservationSource::VendorCache));
        CHECK(lane.metered.has_value() == (s == ObservationSource::Metered));

        lane.drop(s);
        CHECK_FALSE(lane.sensed.has_value());
        CHECK_FALSE(lane.spoolman.has_value());
        CHECK_FALSE(lane.local_user.has_value());
        CHECK_FALSE(lane.vendor_cache.has_value());
        CHECK_FALSE(lane.metered.has_value());
    }
}

namespace {
/// A lane_data record as it arrives off the wire, so the lock keys are present
/// or absent exactly as a co-author or a legacy write left them.
helix::ams::FilamentSlotOverride record_from(const nlohmann::json& j) {
    auto parsed = helix::ams::from_lane_data_record(j);
    REQUIRE(parsed.has_value());
    return parsed->second;
}

/// A filament_slot_overrides.json entry, parsed with the cache's own reader.
/// The cache spells almost every field differently from a lane_data record
/// (color_rgb as a bare integer rather than a "#RRGGBB" string, spoolman_id
/// rather than spool_id, no vendor/vendor_name alias for brand), so a test
/// standing in for a real cache document has to go through from_json, not
/// from_lane_data_record, or it is not exercising the shape it claims to.
helix::ams::FilamentSlotOverride cache_record_from(const nlohmann::json& j) {
    return helix::ams::from_json(j);
}
} // namespace

TEST_CASE("a linked record is the server's declaration, locks unread", "[lane][ingest]") {
    const nlohmann::json wire = {
        {"lane", "0"},
        {"color", "#A4B2BC"},
        {"spool_id", 7},
        {"helix_locked_color", true},
        {"helix_locked_material", true},
        {"material", "PETG"},
    };
    const auto rec = record_from(wire);

    CHECK(classify_declaration(rec) == ObservationSource::Spoolman);

    const auto obs = declared_from_record(rec);
    CHECK(obs.source == ObservationSource::Spoolman);
    CHECK(obs.color_rgb == 0xA4B2BC);
    CHECK(obs.spoolman_id == 7);
}

TEST_CASE("an unlinked record with a real lock is the user's declaration", "[lane][ingest]") {
    const nlohmann::json wire = {
        {"lane", "1"},
        {"color", "#BCBCBC"},
        {"helix_locked_color", true},
    };
    const auto rec = record_from(wire);

    CHECK(classify_declaration(rec) == ObservationSource::LocalUser);
    // The observation has to carry the same verdict, not merely the colour: a
    // classifier that files a person's locked colour under VendorCache is the
    // stale-cache-reads-as-a-choice failure this model exists to delete.
    CHECK(declared_from_record(rec).source == ObservationSource::LocalUser);
    CHECK(declared_from_record(rec).color_rgb == 0xBCBCBC);
}

TEST_CASE("an unlinked record with no lock key is remembered, not declared", "[lane][ingest]") {
    // A legacy record carrying a colour and no lock key declares nothing: only
    // a key that is actually present is a human's signature.
    const nlohmann::json wire = {
        {"lane", "2"},
        {"color", "#ED2C2C"},
    };
    const auto rec = record_from(wire);
    CHECK_FALSE(helix::ams::declares_color(rec));

    CHECK(classify_declaration(rec) == ObservationSource::Remembered);
    CHECK(declared_from_record(rec).source == ObservationSource::Remembered);
}

TEST_CASE("a record with a zero spool id is unlinked", "[lane][ingest]") {
    const nlohmann::json wire = {
        {"lane", "3"},
        {"color", "#000000"},
        {"spool_id", 0},
        {"helix_locked_color", true},
    };
    const auto rec = record_from(wire);

    CHECK(classify_declaration(rec) == ObservationSource::LocalUser);
    // Pure black survives the round trip. It is a colour, not an absent one.
    CHECK(declared_from_record(rec).color_rgb == 0x000000u);
}

TEST_CASE("a record with no colour does not claim one", "[lane][ingest]") {
    const nlohmann::json wire = {
        {"lane", "0"},
        {"material", "PLA"},
    };
    const auto rec = record_from(wire);

    // The wire carries no lock key at all, so the material the record holds is
    // remembered, not declared.
    CHECK(classify_declaration(rec) == ObservationSource::Remembered);

    const auto obs = declared_from_record(rec);
    CHECK(obs.material == "PLA");
    CHECK_FALSE(obs.color_rgb.has_value());
}

TEST_CASE("a record carrying the default-slot sentinel does not declare a colour",
          "[lane][ingest]") {
    // "#808080" round-trips through from_lane_data_record with color_set true
    // and color_rgb == AMS_DEFAULT_SLOT_COLOR, indistinguishable in the struct
    // from a real grey. The sentinel means "no colour reading" everywhere else
    // it is read, and this is the third place that has to honour that.
    const nlohmann::json wire = {
        {"lane", "4"},
        {"color", "#808080"},
    };
    const auto rec = record_from(wire);
    REQUIRE(rec.color_set);
    REQUIRE(rec.color_rgb == helix::AMS_DEFAULT_SLOT_COLOR);

    const auto obs = declared_from_record(rec);
    CHECK_FALSE(obs.color_rgb.has_value());
}

TEST_CASE("a record carrying only material observes nothing else", "[lane][ingest]") {
    // Every other field on FilamentSlotOverride defaults to something that
    // looks like a value (empty string, 0, -1.0f): an absent field and a
    // field declared empty/zero must read as two different statements, or
    // Observation's whole "nullopt means not observed" contract is void.
    const nlohmann::json wire = {
        {"lane", "6"},
        {"material", "PLA"},
    };
    const auto rec = record_from(wire);
    const auto obs = declared_from_record(rec);

    REQUIRE(obs.material.has_value());
    CHECK(*obs.material == "PLA");
    CHECK_FALSE(obs.color_rgb.has_value());
    CHECK_FALSE(obs.color_name.has_value());
    CHECK_FALSE(obs.brand.has_value());
    CHECK_FALSE(obs.spool_name.has_value());
    CHECK_FALSE(obs.catalog_id.has_value());
    CHECK_FALSE(obs.product_name.has_value());
    CHECK_FALSE(obs.spoolman_id.has_value());
    CHECK_FALSE(obs.spoolman_vendor_id.has_value());
    CHECK_FALSE(obs.remaining_weight_g.has_value());
    CHECK_FALSE(obs.total_weight_g.has_value());
}

TEST_CASE("a fully populated record observes every field it carries", "[lane][ingest]") {
    const nlohmann::json wire = {
        {"lane", "7"},
        {"color", "#112233"},
        {"color_name", "Galaxy Black"},
        {"material", "ABS"},
        {"vendor", "Sunlu"},
        {"spool_name", "Reel 5"},
        {"helix_catalog_id", "cat-42"},
        {"helix_product_name", "ABS Marble"},
        {"spoolman_vendor_id", 3},
        {"remaining_weight_g", 512.0},
        {"total_weight_g", 1000.0},
    };
    const auto rec = record_from(wire);
    const auto obs = declared_from_record(rec);

    REQUIRE(obs.color_rgb.has_value());
    CHECK(*obs.color_rgb == 0x112233u);
    REQUIRE(obs.color_name.has_value());
    CHECK(*obs.color_name == "Galaxy Black");
    REQUIRE(obs.material.has_value());
    CHECK(*obs.material == "ABS");
    REQUIRE(obs.brand.has_value());
    CHECK(*obs.brand == "Sunlu");
    REQUIRE(obs.spool_name.has_value());
    CHECK(*obs.spool_name == "Reel 5");
    REQUIRE(obs.catalog_id.has_value());
    CHECK(*obs.catalog_id == "cat-42");
    REQUIRE(obs.product_name.has_value());
    CHECK(*obs.product_name == "ABS Marble");
    REQUIRE(obs.spoolman_vendor_id.has_value());
    CHECK(*obs.spoolman_vendor_id == 3);
    REQUIRE(obs.remaining_weight_g.has_value());
    CHECK(*obs.remaining_weight_g == Catch::Approx(512.0f));
    REQUIRE(obs.total_weight_g.has_value());
    CHECK(*obs.total_weight_g == Catch::Approx(1000.0f));
}

// ============================================================================
// sources_from_record: splitting a legacy stored record across sources
// ============================================================================

TEST_CASE("A stored record carrying a spool binding migrates to the server's rung",
          "[lane][ingest]") {
    // Verbatim in shape to a lane migrating off the old scheme: a Spoolman
    // binding, both locks true, and a stored colour equal to the linked
    // spool's own.
    const nlohmann::json wire = {{"lane", "0"},
                                 {"spool_id", 7},
                                 {"color", "#FFFFFF"},
                                 {"helix_material", "PETG"},
                                 {"vendor", "Kingroon"},
                                 {"helix_locked_color", true},
                                 {"helix_locked_material", true}};
    const auto rec = record_from(wire);

    const auto sources = sources_from_record(rec, wire);

    // The deciding field is color_rgb, and the deciding question is which
    // source holds it. Both locks are true, so a classifier that read them
    // would put white on the user's rung, where it would outrank the server
    // permanently. It has to land on Spoolman's.
    REQUIRE(sources.spoolman.has_value());
    REQUIRE(sources.spoolman->color_rgb.has_value());
    CHECK(*sources.spoolman->color_rgb == 0xFFFFFF);
    CHECK_FALSE(sources.local_user.has_value());
}

TEST_CASE("A linked record's filament definition id files with the binding it belongs to",
          "[lane][ingest][1632]") {
    SECTION("a record naming a spool states its filament id on the server's rung") {
        const nlohmann::json wire = {
            {"lane", "0"}, {"spool_id", 7}, {"helix_spoolman_filament_id", 55}};
        const auto rec = record_from(wire);

        const auto sources = sources_from_record(rec, wire);

        // The filament id is the spool's own statement about itself, so it
        // travels with the binding: the server's rung is where the poll files
        // the same field, and a record read back at load must land there too
        // or a restart with the server down would show no filament id.
        REQUIRE(sources.spoolman.has_value());
        REQUIRE(sources.spoolman->spoolman_filament_id.has_value());
        CHECK(*sources.spoolman->spoolman_filament_id == 55);
    }
    SECTION("a record written before the key existed reads none") {
        const nlohmann::json wire = {{"lane", "0"}, {"spool_id", 7}};
        const auto rec = record_from(wire);

        const auto sources = sources_from_record(rec, wire);

        REQUIRE(sources.spoolman.has_value());
        CHECK_FALSE(sources.spoolman->spoolman_filament_id.has_value());
    }
    SECTION("a record naming no spool states no filament id anywhere") {
        // Without the binding the id means nothing: it identifies a filament
        // of a spool this lane does not hold, so no rung files it.
        const nlohmann::json wire = {{"lane", "0"}, {"helix_spoolman_filament_id", 55}};
        const auto rec = record_from(wire);

        const auto sources = sources_from_record(rec, wire);

        CHECK_FALSE(sources.spoolman.has_value());
        CHECK_FALSE(sources.local_user.has_value());
        CHECK_FALSE(sources.remembered.has_value());
    }
}

TEST_CASE("An unlinked stored record without the lock key is remembered, not declared",
          "[lane][ingest]") {
    // A legacy record with a colour and no key declares nothing, so a legacy
    // lane migrates as what the store remembers rather than as user-authored.
    const nlohmann::json wire = {{"lane", "2"}, {"color", "#ED2C2C"}, {"helix_material", "PLA"}};
    const auto rec = record_from(wire);

    const auto sources = sources_from_record(rec, wire);

    REQUIRE(sources.remembered.has_value());
    REQUIRE(sources.remembered->color_rgb.has_value());
    CHECK(*sources.remembered->color_rgb == 0xED2C2C);
    CHECK(sources.remembered->material == "PLA");
    CHECK_FALSE(sources.local_user.has_value());
}

TEST_CASE("An unlinked stored record with the lock key set true is the user's own",
          "[lane][ingest]") {
    const nlohmann::json wire = {{"lane", "2"},
                                 {"color", "#BCBCBC"},
                                 {"helix_material", "PLA"},
                                 {"helix_locked_color", true},
                                 {"helix_locked_material", false}};
    const auto rec = record_from(wire);

    const auto sources = sources_from_record(rec, wire);

    // color_rgb is the deciding field for the colour lock and material is the
    // deciding field for the material lock. They classify independently, so
    // this record splits across two sources.
    REQUIRE(sources.local_user.has_value());
    REQUIRE(sources.local_user->color_rgb.has_value());
    CHECK(*sources.local_user->color_rgb == 0xBCBCBC);
    CHECK_FALSE(sources.local_user->material.has_value());

    REQUIRE(sources.remembered.has_value());
    REQUIRE(sources.remembered->material.has_value());
    CHECK(*sources.remembered->material == "PLA");
    CHECK_FALSE(sources.remembered->color_rgb.has_value());
}

TEST_CASE("An unlinked stored record with the lock key set false is remembered", "[lane][ingest]") {
    // The explicit false is the auto-mirror's own signature. It must not be
    // treated like an absent key that happens to read the same in the struct.
    const nlohmann::json wire = {{"lane", "1"},
                                 {"color", "#112233"},
                                 {"helix_material", "ABS"},
                                 {"helix_locked_color", false},
                                 {"helix_locked_material", false}};
    const auto rec = record_from(wire);

    const auto sources = sources_from_record(rec, wire);

    CHECK_FALSE(sources.local_user.has_value());
    REQUIRE(sources.remembered.has_value());
    CHECK(sources.remembered->color_rgb == 0x112233);
}

TEST_CASE("A catalog pick is always the user's, because firmware cannot make one",
          "[lane][ingest]") {
    const nlohmann::json wire = {{"lane", "0"},
                                 {"helix_material", "PLA"},
                                 {"helix_catalog_id", "sunlu-pla-plus-2-0"},
                                 {"helix_product_name", "PLA+ 2.0"}};
    const auto rec = record_from(wire);

    const auto sources = sources_from_record(rec, wire);

    // catalog_id is the deciding field: no lock key is present, so the
    // material beside it lands on the cache, and the catalog pick must not
    // follow it there.
    REQUIRE(sources.local_user.has_value());
    REQUIRE(sources.local_user->catalog_id.has_value());
    CHECK(*sources.local_user->catalog_id == "sunlu-pla-plus-2-0");
    CHECK(*sources.local_user->product_name == "PLA+ 2.0");
    REQUIRE(sources.remembered.has_value());
    CHECK(sources.remembered->material == "PLA");
}

TEST_CASE("The two lock-key spellings read the same document differently", "[lane][ingest]") {
    // lane_data spells its lock keys with the helix_ prefix. Reading the same
    // wire under the LocalCache spelling has to miss the lock entirely: it is
    // looking for a key ("user_locked_color") this document never wrote.
    const nlohmann::json wire = {{"lane", "0"}, {"color", "#BCBCBC"}, {"helix_locked_color", true}};
    const auto rec = record_from(wire);

    const auto as_lane_data = sources_from_record(rec, wire, LegacyLockKeys::LaneData);
    REQUIRE(as_lane_data.local_user.has_value());
    CHECK(*as_lane_data.local_user->color_rgb == 0xBCBCBC);

    // The spelling is read when the parser builds the declared set, so the
    // local cache's reading of this document is the set that spelling gives.
    helix::ams::FilamentSlotOverride as_local_record = rec;
    as_local_record.declared =
        helix::ams::declared_fields_on_load(wire, LegacyLockKeys::LocalCache, rec);
    const auto as_local_remembered =
        sources_from_record(as_local_record, wire, LegacyLockKeys::LocalCache);
    CHECK_FALSE(as_local_remembered.local_user.has_value());
    REQUIRE(as_local_remembered.remembered.has_value());
    CHECK(*as_local_remembered.remembered->color_rgb == 0xBCBCBC);
}

TEST_CASE("A remembered record carrying a brand is not dropped by its own parser",
          "[lane][ingest]") {
    // filament_slot_overrides.json writes the bare "brand" key (to_json);
    // from_lane_data_record instead reads "vendor" / "vendor_name" and would
    // read this document's brand as absent. Parsing it with from_json, the
    // reader that actually knows this shape, is what keeps the brand.
    const nlohmann::json wire = {{"brand", "Kingroon"},
                                 {"color_rgb", 0x3355FF},
                                 {"material", "PETG"},
                                 {"user_locked_color", true}};
    const auto rec = cache_record_from(wire);
    REQUIRE(rec.brand == "Kingroon");

    const auto sources = sources_from_record(rec, wire, LegacyLockKeys::LocalCache);

    // The record names no declared set, so its brand answers to the colour its
    // true lock key declares: that declaration is the evidence a person edited
    // this record, so the brand is theirs. No lock key names material, so
    // material is the cache's.
    REQUIRE(sources.local_user.has_value());
    REQUIRE(sources.local_user->brand.has_value());
    CHECK(*sources.local_user->brand == "Kingroon");
    REQUIRE(sources.remembered.has_value());
    REQUIRE(sources.remembered->material.has_value());
    CHECK(*sources.remembered->material == "PETG");
}

TEST_CASE("Weights migrate to the meter, never to a declaration", "[lane][ingest]") {
    const nlohmann::json wire = {{"lane", "0"},
                                 {"helix_material", "PLA"},
                                 {"remaining_weight_g", 218.0},
                                 {"total_weight_g", 1000.0}};
    const auto rec = record_from(wire);

    const auto sources = sources_from_record(rec, wire);

    REQUIRE(sources.metered.has_value());
    CHECK(sources.metered->remaining_weight_g == Catch::Approx(218.0f));
    CHECK(sources.metered->total_weight_g == Catch::Approx(1000.0f));
    // A weight is not evidence of presence.
    CHECK_FALSE(sources.metered->present.has_value());
}

TEST_CASE("A linked record's weight still migrates to the meter, not to Spoolman",
          "[lane][ingest]") {
    const nlohmann::json wire = {{"lane", "0"}, {"spool_id", 7}, {"remaining_weight_g", 42.0}};
    const auto rec = record_from(wire);

    const auto sources = sources_from_record(rec, wire);

    REQUIRE(sources.metered.has_value());
    CHECK(sources.metered->remaining_weight_g == Catch::Approx(42.0f));
    REQUIRE(sources.spoolman.has_value());
    CHECK_FALSE(sources.spoolman->remaining_weight_g.has_value());
}

TEST_CASE("A linked record's catalog pick is the user's, not the server's", "[lane][ingest]") {
    const nlohmann::json wire = {{"lane", "0"},
                                 {"spool_id", 7},
                                 {"color", "#1A1A2E"},
                                 {"vendor", "Polymaker"},
                                 {"helix_catalog_id", "polymaker-polyterra-pla-charcoal"},
                                 {"helix_product_name", "PolyTerra PLA Charcoal"}};
    const auto rec = record_from(wire);

    const auto sources = sources_from_record(rec, wire);

    // Spoolman has no catalog product to state, and a fetch replaces the
    // server's record whole, so a pick filed there is gone after the first one.
    REQUIRE(sources.spoolman.has_value());
    CHECK(sources.spoolman->brand == "Polymaker");
    CHECK(sources.spoolman->color_rgb == 0x1A1A2EU);
    CHECK_FALSE(sources.spoolman->catalog_id.has_value());
    CHECK_FALSE(sources.spoolman->product_name.has_value());

    REQUIRE(sources.local_user.has_value());
    CHECK(sources.local_user->catalog_id == "polymaker-polyterra-pla-charcoal");
    CHECK(sources.local_user->product_name == "PolyTerra PLA Charcoal");
}

TEST_CASE("Nothing a stored record migrates is ever evidence that a lane is occupied",
          "[lane][ingest]") {
    // The scenario the split exists for: an ejected lane whose stored record
    // still holds colour, material and a spool binding.
    const nlohmann::json wire = {{"lane", "0"},
                                 {"spool_id", 7},
                                 {"color", "#FFFFFF"},
                                 {"helix_material", "PETG"},
                                 {"helix_locked_color", true},
                                 {"helix_locked_material", true}};
    const auto rec = record_from(wire);

    const auto sources = sources_from_record(rec, wire);

    CHECK_FALSE(sources.sensed.has_value());
    // A stored record is a declaration, never a sensor reading, so it leaves
    // presence unobserved rather than asserting the lane is empty.
    CHECK_FALSE(helix::ams::resolve(sources).present.has_value());
}

// ============================================================================
// Colour and material authorship on load. A record written before its declared
// set could name either field says who chose them only through its lock keys.
// ============================================================================

namespace {

enum class LockKey { True, False, Absent };

struct LegacyDocument {
    nlohmann::json wire;
    helix::ams::FilamentSlotOverride record;
};

/// A record holding colour 0x3355FF and material PETG, spelled the way @p keys
/// names its document, bound to @p spool_id, with both lock keys set to @p lock.
LegacyDocument legacy_document(LegacyLockKeys keys, int spool_id, LockKey lock) {
    const bool lane_data = keys == LegacyLockKeys::LaneData;
    nlohmann::json wire;
    if (lane_data) {
        wire = {{"lane", "0"}, {"color", "#3355FF"}, {"helix_material", "PETG"}};
        if (spool_id > 0) {
            wire["spool_id"] = spool_id;
        }
    } else {
        wire = {{"color_rgb", 0x3355FF},
                {"color_set", true},
                {"material", "PETG"},
                {"spoolman_id", spool_id}};
    }
    if (lock != LockKey::Absent) {
        const bool value = lock == LockKey::True;
        wire[lane_data ? "helix_locked_color" : "user_locked_color"] = value;
        wire[lane_data ? "helix_locked_material" : "user_locked_material"] = value;
    }
    return {wire, lane_data ? record_from(wire) : cache_record_from(wire)};
}

} // namespace

TEST_CASE("a release 1.0 record's lock keys decide authorship only on an unlinked lane",
          "[lane][ingest][migration]") {
    using helix::ams::declares_color;
    using helix::ams::declares_material;

    const LegacyLockKeys keys = GENERATE(LegacyLockKeys::LaneData, LegacyLockKeys::LocalCache);
    INFO((keys == LegacyLockKeys::LaneData ? "lane_data spelling" : "local cache spelling"));

    SECTION("an unlinked record whose keys say true declares both fields") {
        const LegacyDocument doc = legacy_document(keys, 0, LockKey::True);
        CHECK(declares_color(doc.record));
        CHECK(declares_material(doc.record));

        const auto sources = sources_from_record(doc.record, doc.wire, keys);
        REQUIRE(sources.local_user.has_value());
        CHECK(sources.local_user->color_rgb == 0x3355FFu);
        CHECK(sources.local_user->material == "PETG");
        CHECK_FALSE(sources.remembered.has_value());
    }

    SECTION("an unlinked record whose keys are missing or false declares neither") {
        const LockKey lock = GENERATE(LockKey::False, LockKey::Absent);
        INFO((lock == LockKey::False ? "keys false" : "keys absent"));
        const LegacyDocument doc = legacy_document(keys, 0, lock);
        CHECK_FALSE(declares_color(doc.record));
        CHECK_FALSE(declares_material(doc.record));

        const auto sources = sources_from_record(doc.record, doc.wire, keys);
        CHECK_FALSE(sources.local_user.has_value());
        REQUIRE(sources.remembered.has_value());
        CHECK(sources.remembered->color_rgb == 0x3355FFu);
        CHECK(sources.remembered->material == "PETG");
    }

    SECTION("a linked record declares neither whatever its keys say") {
        // A release 1.0 writer set these keys on links and meter flushes alike,
        // so on a linked record they record nothing a person chose.
        const LockKey lock = GENERATE(LockKey::True, LockKey::False);
        INFO((lock == LockKey::True ? "keys true" : "keys false"));
        const LegacyDocument doc = legacy_document(keys, 7, lock);
        CHECK_FALSE(declares_color(doc.record));
        CHECK_FALSE(declares_material(doc.record));

        const auto sources = sources_from_record(doc.record, doc.wire, keys);
        CHECK_FALSE(sources.local_user.has_value());
        REQUIRE(sources.spoolman.has_value());
        CHECK(sources.spoolman->color_rgb == 0x3355FFu);
        CHECK(sources.spoolman->material == "PETG");
    }
}

TEST_CASE("a record whose declared set could not name colour keeps its lock-key reading",
          "[lane][ingest][migration]") {
    // The shape a build whose declared set held only brand, spool name and
    // vendor id wrote: a set naming none of colour and material, beside the
    // lock keys that carried those two.
    using helix::ams::declares_color;
    using helix::ams::declares_material;

    nlohmann::json wire = {{"lane", "0"},
                           {"color", "#3355FF"},
                           {"helix_material", "PETG"},
                           {"vendor", "Hatchbox"},
                           {"helix_locked_color", true},
                           {"helix_locked_material", true},
                           {"helix_declared", nlohmann::json::array({"brand"})}};

    SECTION("an unlinked record's true keys are the user's colour and material") {
        const auto rec = record_from(wire);
        CHECK(declares_color(rec));
        CHECK(declares_material(rec));

        const auto sources = sources_from_record(rec, wire);
        REQUIRE(sources.local_user.has_value());
        CHECK(sources.local_user->color_rgb == 0x3355FFu);
        CHECK(sources.local_user->material == "PETG");
        CHECK(sources.local_user->brand == "Hatchbox");
    }

    SECTION("a linked record's keys are not read") {
        wire["spool_id"] = 7;
        const auto rec = record_from(wire);
        CHECK_FALSE(declares_color(rec));
        CHECK_FALSE(declares_material(rec));

        const auto sources = sources_from_record(rec, wire);
        CHECK_FALSE(sources.local_user.has_value());
        REQUIRE(sources.spoolman.has_value());
        CHECK(sources.spoolman->color_rgb == 0x3355FFu);
        CHECK(sources.spoolman->material == "PETG");
    }
}

TEST_CASE("a linked record whose declared set names its colour files that colour as the user's",
          "[lane][ingest][1653]") {
    // The colour ladder puts a person above the server, and the declared set is
    // what says the colour is a person's rather than the spool's.
    const LockKey lock = GENERATE(LockKey::True, LockKey::False, LockKey::Absent);
    INFO((lock == LockKey::True    ? "lock key true"
          : lock == LockKey::False ? "lock key false"
                                   : "lock key absent"));

    nlohmann::json wire = {{"lane", "0"},
                           {"spool_id", 7},
                           {"color", "#3355FF"},
                           {"color_name", "Cobalt"},
                           {"helix_material", "PETG"},
                           {"vendor", "Kingroon"},
                           {"helix_declared", nlohmann::json::array({"color_rgb"})}};
    if (lock != LockKey::Absent) {
        wire["helix_locked_color"] = lock == LockKey::True;
    }
    const auto rec = record_from(wire);
    REQUIRE(helix::ams::declares_color(rec));

    const auto sources = sources_from_record(rec, wire);

    REQUIRE(sources.local_user.has_value());
    REQUIRE(sources.local_user->color_rgb.has_value());
    CHECK(*sources.local_user->color_rgb == 0x3355FFu);
    CHECK(sources.local_user->color_name == "Cobalt");

    // The rest of the identity is still the server's, and the colour has left
    // the server's rung rather than standing on both.
    REQUIRE(sources.spoolman.has_value());
    CHECK_FALSE(sources.spoolman->color_rgb.has_value());
    CHECK_FALSE(sources.spoolman->color_name.has_value());
    CHECK(sources.spoolman->brand == "Kingroon");
    CHECK(sources.spoolman->material == "PETG");
    CHECK(sources.spoolman->spoolman_id == 7);

    CHECK(helix::ams::resolve(sources).color_rgb == 0x3355FFu);
}

TEST_CASE("a linked record names material in vain", "[lane][ingest][1653]") {
    // The colour is the one field a linked record's set is read for. A spool
    // owns its material and brand, so a set naming them changes nothing.
    const nlohmann::json wire = {
        {"lane", "0"},          {"spool_id", 7},
        {"color", "#3355FF"},   {"helix_material", "PETG"},
        {"vendor", "Kingroon"}, {"helix_declared", nlohmann::json::array({"material", "brand"})}};
    const auto rec = record_from(wire);
    REQUIRE(helix::ams::declares_material(rec));

    const auto sources = sources_from_record(rec, wire);

    CHECK_FALSE(sources.local_user.has_value());
    REQUIRE(sources.spoolman.has_value());
    CHECK(sources.spoolman->material == "PETG");
    CHECK(sources.spoolman->brand == "Kingroon");
    CHECK(sources.spoolman->color_rgb == 0x3355FFu);
}
