// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// Resilience of AFC state/narration string matching against upstream rewording.
//
// AFC-Klipper-Add-On v1.2.0 renamed its TOOL_SWAP state from "Tool swap" to
// "ToolSwap" and added ToolDock/ToolPickup. Neither spelling was recognized, so
// a toolchange reported AmsAction::IDLE and the raw camelCase string reached the
// screen. These tests pin the normalized matching that makes the whole class of
// rename a non-event, and the humanized display fallback for states we have
// never seen.

#include "test_helpers/afc_test_access.h"
#include "ams_backend_afc.h"
#include "ams_types.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace helix {

class AfcStateStringHelper : public AmsBackendAfc {
  public:
    AfcStateStringHelper() : AmsBackendAfc(nullptr, nullptr) {}

    void initialize_test_lanes(int count) {
        std::vector<std::string> names;
        for (int i = 0; i < count; ++i) {
            names.push_back("lane" + std::to_string(i + 1));
        }
        AfcTestAccess::initialize_slots(*this, names);
    }

    void feed_afc_state(const nlohmann::json& afc_data) {
        nlohmann::json notification;
        nlohmann::json params;
        params["AFC"] = afc_data;
        notification["params"] = nlohmann::json::array({params, 0.0});
        handle_status_update(notification);
    }

    void feed_status_update(const nlohmann::json& params_inner) {
        nlohmann::json notification;
        notification["params"] = nlohmann::json::array({params_inner, 0.0});
        handle_status_update(notification);
    }

    // Set extruder names through the production path so AFC_extruder objects
    // are subsequently parsed.
    void setup_extruder_names(const std::vector<std::string>& names) {
        nlohmann::json afc_data;
        afc_data["extruders"] = names;
        afc_data["current_state"] = "Idle";
        feed_afc_state(afc_data);
    }

    const AmsSystemInfo& info() const {
        return system_info_;
    }

    const std::unordered_map<std::string, AfcToolState>& tool_states() const {
        return AfcTestAccess::tool_states(*this);
    }

    using AmsBackendAfc::match_narration_phase;
};
} // namespace helix

// ============================================================================
// ams_action_from_string() — normalized matching
// ============================================================================

TEST_CASE("AFC v1.2.0 toolchanger states map to a busy action", "[afc][state][resilience]") {
    // These three are new in AFC v1.2.0 (extras/AFC.py State enum). Before the
    // fix all of them fell through to IDLE, which defeated the toast
    // suppression that gates on an active operation.
    REQUIRE(ams_action_from_string("ToolSwap") == AmsAction::SELECTING);
    REQUIRE(ams_action_from_string("ToolDock") == AmsAction::SELECTING);
    REQUIRE(ams_action_from_string("ToolPickup") == AmsAction::SELECTING);
}

TEST_CASE("AFC pre-1.2.0 'Tool swap' spelling still maps", "[afc][state][resilience]") {
    // The whole point of normalizing: the rename must not matter in either
    // direction, so an older AFC keeps working after we adopt the new spelling.
    // recognized==true is the load-bearing assertion — it proves the token was
    // matched EXACTLY after normalization rather than rescued by the fuzzy
    // has("swap") fallback, which would also return SELECTING.
    bool recognized = false;
    REQUIRE(ams_action_from_string("Tool swap", &recognized) == AmsAction::SELECTING);
    REQUIRE(recognized == true);
}

TEST_CASE("State matching ignores case, spaces and separators", "[afc][state][resilience]") {
    // Any punctuation/case variant of the same word collapses to one token, so
    // a future "TOOL_SWAP" or "tool-swap" needs no code change. Each must be an
    // EXACT match post-normalization (recognized==true), not a fuzzy rescue.
    for (const char* variant :
         {"TOOL_SWAP", "tool-swap", "toolSwap", "Tool  Swap", "tool.swap", "  ToolSwap  "}) {
        bool recognized = false;
        CAPTURE(variant);
        REQUIRE(ams_action_from_string(variant, &recognized) == AmsAction::SELECTING);
        REQUIRE(recognized == true);
    }
}

TEST_CASE("AFC motion and lifecycle states map to sensible actions", "[afc][state][resilience]") {
    REQUIRE(ams_action_from_string("Ejecting") == AmsAction::UNLOADING);
    REQUIRE(ams_action_from_string("Moving") == AmsAction::SELECTING);
    REQUIRE(ams_action_from_string("Restoring") == AmsAction::SELECTING);
    REQUIRE(ams_action_from_string("Initialized") == AmsAction::IDLE);
}

TEST_CASE("Unload is matched before load in fuzzy fallback", "[afc][state][resilience]") {
    // "unloading" contains "loading"; ordering must not misreport an unload as
    // a load. This is the sharpest edge in substring-based fallback matching.
    REQUIRE(ams_action_from_string("Tool Unloading") == AmsAction::UNLOADING);
    REQUIRE(ams_action_from_string("ToolUnload") == AmsAction::UNLOADING);
    REQUIRE(ams_action_from_string("Lane Unloading") == AmsAction::UNLOADING);
    // ...but a genuine load still resolves to LOADING.
    REQUIRE(ams_action_from_string("Tool Loading") == AmsAction::LOADING);
    REQUIRE(ams_action_from_string("HUB Loading") == AmsAction::LOADING);
}

TEST_CASE("Existing Happy Hare action vocabulary is unchanged", "[afc][state][resilience]") {
    // Regression guard: normalization must not disturb the strings that
    // already worked before this change.
    REQUIRE(ams_action_from_string("Idle") == AmsAction::IDLE);
    REQUIRE(ams_action_from_string("Loading") == AmsAction::LOADING);
    REQUIRE(ams_action_from_string("Unloading") == AmsAction::UNLOADING);
    REQUIRE(ams_action_from_string("Selecting") == AmsAction::SELECTING);
    REQUIRE(ams_action_from_string("Homing") == AmsAction::RESETTING);
    REQUIRE(ams_action_from_string("Resetting") == AmsAction::RESETTING);
    REQUIRE(ams_action_from_string("Forming Tip") == AmsAction::FORMING_TIP);
    REQUIRE(ams_action_from_string("Cutting") == AmsAction::CUTTING);
    REQUIRE(ams_action_from_string("Cutting Tip") == AmsAction::CUTTING);
    REQUIRE(ams_action_from_string("Cutting Filament") == AmsAction::CUTTING);
    REQUIRE(ams_action_from_string("Loading Ext") == AmsAction::LOADING);
    REQUIRE(ams_action_from_string("Exiting Ext") == AmsAction::UNLOADING);
    REQUIRE(ams_action_from_string("Heating") == AmsAction::HEATING);
    REQUIRE(ams_action_from_string("Checking") == AmsAction::CHECKING);
    REQUIRE(ams_action_from_string("Purging") == AmsAction::PURGING);
    REQUIRE(ams_action_from_string("Paused") == AmsAction::PAUSED);
    REQUIRE(ams_action_from_string("Error") == AmsAction::ERROR);
}

TEST_CASE("Unrecognized states report unrecognized and fall back to IDLE",
          "[afc][state][resilience]") {
    bool recognized = true;
    REQUIRE(ams_action_from_string("Blorptastic", &recognized) == AmsAction::IDLE);
    REQUIRE(recognized == false);

    recognized = false;
    REQUIRE(ams_action_from_string("ToolSwap", &recognized) == AmsAction::SELECTING);
    REQUIRE(recognized == true);

    // Empty string is not a drift signal — it is the normal "no state" case.
    recognized = true;
    REQUIRE(ams_action_from_string("", &recognized) == AmsAction::IDLE);
    REQUIRE(recognized == true);
}

// ============================================================================
// Display string — never show a raw wire token
// ============================================================================

TEST_CASE("AFC toolchange state produces a human detail string, not camelCase",
          "[afc][state][resilience]") {
    AfcStateStringHelper afc;
    afc.initialize_test_lanes(4);

    afc.feed_afc_state({{"current_state", "ToolSwap"}});

    REQUIRE(afc.info().action == AmsAction::SELECTING);
    // The raw camelCase wire token must never reach operation_detail, which is
    // passed through to the UI verbatim (ams_state.cpp recompute_action_detail).
    REQUIRE(afc.info().operation_detail != "ToolSwap");
    REQUIRE(afc.info().operation_detail == "Tool swap");
}

TEST_CASE("Unknown future AFC state is humanized rather than shown raw",
          "[afc][state][resilience]") {
    AfcStateStringHelper afc;
    afc.initialize_test_lanes(4);

    afc.feed_afc_state({{"current_state", "SomeFutureState"}});

    REQUIRE(afc.info().operation_detail == "Some future state");
}

TEST_CASE("Snake_case and screaming states humanize cleanly", "[afc][state][resilience]") {
    AfcStateStringHelper afc;
    afc.initialize_test_lanes(4);

    afc.feed_afc_state({{"current_state", "SOME_LOUD_STATE"}});
    REQUIRE(afc.info().operation_detail == "Some loud state");

    afc.feed_afc_state({{"current_state", "purging_bucket"}});
    REQUIRE(afc.info().operation_detail == "Purging bucket");
}

TEST_CASE("States that already worked keep their existing detail text",
          "[afc][state][resilience]") {
    AfcStateStringHelper afc;
    afc.initialize_test_lanes(4);

    afc.feed_afc_state({{"current_state", "Loading"}});
    REQUIRE(afc.info().operation_detail == "Loading");
    REQUIRE(afc.info().action == AmsAction::LOADING);

    afc.feed_afc_state({{"current_state", "Unloading"}});
    REQUIRE(afc.info().operation_detail == "Unloading");
    REQUIRE(afc.info().action == AmsAction::UNLOADING);
}

// ============================================================================
// AFC_extruder toolchanger fields (AFC v1.2.0 #768)
// ============================================================================

TEST_CASE("AFC_extruder toolchanger status fields are parsed", "[afc][toolchange][resilience]") {
    AfcStateStringHelper afc;
    afc.initialize_test_lanes(4);
    afc.setup_extruder_names({"extruder", "extruder1"});

    nlohmann::json params;
    params["AFC_extruder extruder"] = {{"lane_loaded", "lane1"},
                                       {"status", "ToolDock"},
                                       {"next_pickup", false},
                                       {"is_standalone", false}};
    params["AFC_extruder extruder1"] = {{"lane_loaded", nullptr},
                                        {"status", "ToolPickup"},
                                        {"next_pickup", true},
                                        {"is_standalone", true}};
    afc.feed_status_update(params);

    REQUIRE(afc.tool_states().size() == 2);

    const auto& e0 = afc.tool_states().at("extruder");
    REQUIRE(e0.status == "ToolDock");
    REQUIRE(e0.next_pickup == false);
    REQUIRE(e0.is_standalone == false);

    const auto& e1 = afc.tool_states().at("extruder1");
    REQUIRE(e1.status == "ToolPickup");
    REQUIRE(e1.next_pickup == true);
    REQUIRE(e1.is_standalone == true);
}

TEST_CASE("AFC_extruder toolchanger fields default safely when absent",
          "[afc][toolchange][resilience]") {
    // Pre-1.2.0 AFC omits all three. Absence must not be read as "standalone"
    // or "next up", both of which would drive wrong path/tool visuals.
    AfcStateStringHelper afc;
    afc.initialize_test_lanes(4);
    afc.setup_extruder_names({"extruder"});

    nlohmann::json params;
    params["AFC_extruder extruder"] = {{"lane_loaded", "lane1"}};
    afc.feed_status_update(params);

    REQUIRE(afc.tool_states().size() == 1);
    REQUIRE(afc.tool_states().at("extruder").status.empty());
    REQUIRE(afc.tool_states().at("extruder").next_pickup == false);
    REQUIRE(afc.tool_states().at("extruder").is_standalone == false);
}

// ============================================================================
// Narration matching — punctuation resilience
// ============================================================================

TEST_CASE("Narration matching survives punctuation and separator rewording",
          "[afc][narration][resilience]") {
    AfcStateStringHelper afc;

    // Upstream's actual v1.2.0 string (config/macros/Brush.cfg).
    REQUIRE(afc.match_narration_phase("AFC_Brush: Clean Nozzle") == "brush");
    // Plausible rewordings that must not break the step bar.
    REQUIRE(afc.match_narration_phase("AFC Brush - Clean nozzle") == "brush");
    REQUIRE(afc.match_narration_phase("afc_brush:clean") == "brush");
    REQUIRE(afc.match_narration_phase("[AFC_Brush] Clean Nozzle!") == "brush");
}

TEST_CASE("Narration phrases from upstream v1.2.0 still resolve", "[afc][narration][resilience]") {
    AfcStateStringHelper afc;

    // Each verified present in AFC v1.2.0 source on 2026-07-26.
    REQUIRE(afc.match_narration_phase("lane1 is now loaded in toolhead") == "load");
    REQUIRE(afc.match_narration_phase("AFC_Brush: Move to Brush.") == "brush");
    REQUIRE(afc.match_narration_phase("Loading lane: lane2") == "feed");
    REQUIRE(afc.match_narration_phase("Moving to hub") == "feed");
    REQUIRE(afc.match_narration_phase("Unloading lane1") == "unload");
    REQUIRE(afc.match_narration_phase("AFC_Poop: Starting poop") == "poop");
    REQUIRE(afc.match_narration_phase("AFC_Kick: Starting Filament Kick") == "kick");
    REQUIRE(afc.match_narration_phase("AFC_Cut: Cut Filament") == "cut");
}

TEST_CASE("Narration phase precedence is preserved", "[afc][narration][resilience]") {
    AfcStateStringHelper afc;

    // Both of AFC_BRUSH's spellings land on the one brush phase.
    REQUIRE(afc.match_narration_phase("AFC_Brush: Clean Nozzle") == "brush");
    // purge wording belongs to the poop macro, never folded into feed.
    REQUIRE(afc.match_narration_phase("Purging filament") == "poop");
    // "unloading lane1" contains "loading lane" — the unload check must win.
    REQUIRE(afc.match_narration_phase("Unloading lane1") == "unload");
    // load-complete beats a generic "loading" mention.
    REQUIRE(afc.match_narration_phase("lane1 is now loaded in toolhead") == "load");
    // Unrelated chatter matches nothing.
    REQUIRE(afc.match_narration_phase("Klipper state: ready") == std::nullopt);
    REQUIRE(afc.match_narration_phase("") == std::nullopt);
}
