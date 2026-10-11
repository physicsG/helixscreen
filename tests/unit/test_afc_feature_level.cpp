// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_afc_feature_level.cpp
 * @brief AFC v1.2.0 feature detection for the upgrade advisory.
 *
 * The trigger for the "upgrade your AFC" toast is capability, never a version
 * string. AFC has no trustworthy version signal — the `afc-install` namespace
 * has been an orphan since their 7d20db7, `AFC_VERSION` is hand-bumped and sat
 * at 1.1.37 through the whole v1.2.0 release, and v1.2.0's get_status()
 * publishes no version key at all. A live BoxTurtle reported "1.0.0" while
 * actually running v1.1.0.
 *
 * The payloads below are real, captured from one physical 4-lane BoxTurtle
 * (Turtle_1) on 2026-08-09 either side of a v1.1.0 -> v1.2.0 upgrade. That is
 * what makes this a regression test rather than a restatement of the
 * implementation: the expectations come from firmware, not from the parser.
 */

#include "test_helpers/afc_test_access.h"
#include "ams_backend_afc.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

/// Turtle_1 lane2, loaded and Spoolman-linked, on AFC v1.1.0-4-g2921371.
nlohmann::json lane_v1_1_0_loaded() {
    return nlohmann::json{{"name", "lane2"},   {"unit", "Turtle_1"},   {"hub", "Turtle_1"},
                          {"lane", 2},         {"map", "T1"},          {"load", true},
                          {"prep", true},      {"tool_loaded", false}, {"loaded_to_hub", true},
                          {"material", "ASA"}, {"spool_id", 5},        {"color", "#00AEFF"},
                          {"weight", 915.22},  {"extruder_temp", 260}, {"status", "None"}};
}

/// The same lane after upgrading that machine to v1.2.0 (a06f14d).
nlohmann::json lane_v1_2_0_loaded() {
    auto j = lane_v1_1_0_loaded();
    j["filament_name"] = "PolyLite™ ASA Pop Blue";
    j["initial_weight"] = 1000.0;
    j["multi_color_hexes"] = nlohmann::json::array();
    j["bed_temp"] = 100;
    return j;
}

/// Turtle_1 lane4 on v1.2.0 — EMPTY. Every new field is still published.
nlohmann::json lane_v1_2_0_empty() {
    return nlohmann::json{{"name", "lane4"},
                          {"lane", 4},
                          {"load", false},
                          {"prep", false},
                          {"material", ""},
                          {"spool_id", nullptr},
                          {"color", ""},
                          {"weight", 0},
                          {"filament_name", ""},
                          {"initial_weight", 1000},
                          {"multi_color_hexes", nlohmann::json::array()}};
}

} // namespace

TEST_CASE("AFC feature detection separates v1.1.0 from v1.2.0", "[ams][afc][feature-level]") {
    SECTION("a real v1.1.0 lane payload reads as legacy") {
        CHECK_FALSE(AmsBackendAfc::status_has_modern_fields(lane_v1_1_0_loaded()));
    }

    SECTION("a real v1.2.0 lane payload reads as modern") {
        CHECK(AmsBackendAfc::status_has_modern_fields(lane_v1_2_0_loaded()));
    }

    SECTION("an EMPTY v1.2.0 lane still reads as modern") {
        // The fields are unconditional on v1.2.0, not gated on a spool being
        // present. If they were spool-gated, a printer with every lane empty
        // would be misreported as legacy and nagged forever.
        CHECK(AmsBackendAfc::status_has_modern_fields(lane_v1_2_0_empty()));
    }
}

TEST_CASE("AFC feature detection accepts any field of the v1.2.0 block",
          "[ams][afc][feature-level]") {
    // AFC emits filament_name / multi_color_hexes / initial_weight together from
    // a single `if not save_to_file:` block, so any one of them proves the block.
    // Accepting all three keeps this working if upstream reorders or splits it.
    SECTION("filament_name alone") {
        CHECK(AmsBackendAfc::status_has_modern_fields(
            nlohmann::json{{"name", "lane1"}, {"filament_name", "x"}}));
    }
    SECTION("initial_weight alone") {
        CHECK(AmsBackendAfc::status_has_modern_fields(
            nlohmann::json{{"name", "lane1"}, {"initial_weight", 1000}}));
    }
    SECTION("multi_color_hexes alone") {
        CHECK(AmsBackendAfc::status_has_modern_fields(
            nlohmann::json{{"name", "lane1"}, {"multi_color_hexes", nlohmann::json::array()}}));
    }

    SECTION("an empty-valued field still counts — presence is the signal") {
        // filament_name is "" on an empty lane and multi_color_hexes is []. The
        // probe asks whether AFC PUBLISHES the key, not whether it has content.
        CHECK(AmsBackendAfc::status_has_modern_fields(
            nlohmann::json{{"name", "lane4"}, {"filament_name", ""}}));
    }
}

namespace helix {

/// Drives the real status path so the prefix learner can be asserted.
class AfcFeatureLevelHelper : public AmsBackendAfc {
  public:
    AfcFeatureLevelHelper() : AmsBackendAfc(nullptr, nullptr) {}

    /// Feed a whole status frame, exactly as the subscription delivers it.
    void feed_frame(const nlohmann::json& objects) {
        nlohmann::json notification;
        notification["params"] = nlohmann::json::array({objects, 0.0});
        handle_status_update(notification);
    }

    [[nodiscard]] std::string prefix() const {
        return AfcTestAccess::lane_object_prefix(*this);
    }
};
} // namespace helix

TEST_CASE("AFC feature probe learns the lane prefix from a status frame",
          "[ams][afc][feature-level]") {
    // A status frame can only tell us WHICH object name the firmware uses. It
    // can never tell us which fields exist: the subscription is field-scoped
    // (afc_stepper_fields in moonraker_discovery_sequence.cpp) and never asks
    // for filament_name/multi_color_hexes/initial_weight, so those keys are
    // absent from every frame regardless of AFC version. Probing a frame
    // reported "legacy" against a BoxTurtle confirmed on v1.2.0 by direct query.
    // The fields come from probe_feature_level()'s explicit unscoped query.
    SECTION("BoxTurtle publishes AFC_stepper") {
        AfcFeatureLevelHelper afc;
        afc.feed_frame(nlohmann::json{{"AFC_stepper lane1", lane_v1_1_0_loaded()}});
        CHECK(afc.prefix() == "AFC_stepper ");
    }

    SECTION("OpenAMS and friends publish AFC_lane") {
        AfcFeatureLevelHelper afc;
        afc.feed_frame(nlohmann::json{{"AFC_lane lane1", lane_v1_1_0_loaded()}});
        CHECK(afc.prefix() == "AFC_lane ");
    }

    SECTION("the prefix is learnable with NO slot registry") {
        // The learner must not sit inside the `i < slots_.slot_count()` loops —
        // a frame arriving before discovery builds the registry matches nothing
        // there, and the probe would never fire at all.
        AfcFeatureLevelHelper afc; // zero slots
        afc.feed_frame(nlohmann::json{{"AFC_stepper lane3", lane_v1_1_0_loaded()}});
        CHECK(afc.prefix() == "AFC_stepper ");
    }

    SECTION("a frame with no lane object teaches nothing") {
        AfcFeatureLevelHelper afc;
        afc.feed_frame(nlohmann::json{{"AFC", nlohmann::json{{"current_state", "Idle"}}}});
        CHECK(afc.prefix().empty());
    }
}

TEST_CASE("AFC feature detection ignores unrelated fields", "[ams][afc][feature-level]") {
    // A lane carrying plenty of data but none of the v1.2.0 block is legacy.
    // Guards against the probe drifting into "has any rich field" and going
    // permanently quiet on old firmware.
    SECTION("a rich v1.1.0 payload is still legacy") {
        auto j = lane_v1_1_0_loaded();
        j["td1_td"] = "";
        j["endstops"] = "load,hub,tool_start";
        j["buffer_status"] = "Advancing";
        CHECK_FALSE(AmsBackendAfc::status_has_modern_fields(j));
    }

    SECTION("an empty object is legacy, not modern") {
        CHECK_FALSE(AmsBackendAfc::status_has_modern_fields(nlohmann::json::object()));
    }
}
