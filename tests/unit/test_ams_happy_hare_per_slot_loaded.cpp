// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ams_happy_hare_per_slot_loaded.cpp
 * @brief Happy Hare's LOADED stamp, and why it stays on the aggregate rule (#1199).
 *
 * parse_mmu_state() derived the per-gate SlotStatus::LOADED stamp inside the
 * `gate_status` branch and only for gates whose mapped status was AVAILABLE.
 * Two holes followed:
 *
 *  1. gate_status 2 (from_buffer) maps to SlotStatus::FROM_BUFFER, so a
 *     buffered gate feeding the toolhead never showed as loaded.
 *  2. printer.mmu arrives as a delta. `gate` and `filament` are parsed outside
 *     the gate_status branch, so the common toolchange frame — gate + filament
 *     with no gate_status — left the stamp on whichever gate was loaded the
 *     last time a gate's fill state happened to change.
 *
 * The stamp is now re-derived from the cached gate_status array on every mmu
 * frame. Happy Hare still does NOT claim per-slot authority: mmu.gate and
 * mmu.filament are firmware's own values parsed verbatim from one object, so
 * the aggregate pair is the authority here and the per-gate status is derived
 * from it — believing the derivation would only add staleness, and would drop
 * the highlight on a gate that ran out (gate_status 0) while still feeding the
 * toolhead.
 */

#include "ams_backend_happy_hare.h"
#include "ams_types.h"

#include <vector>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using json = nlohmann::json;
using namespace helix;

namespace {

/// Feeds the production status path (handle_status -> parse_mmu_state).
class HappyHarePerSlotLoadedHelper : public AmsBackendHappyHare {
  public:
    HappyHarePerSlotLoadedHelper() : AmsBackendHappyHare(nullptr, nullptr) {}

    /// One printer.mmu delta, exactly as Moonraker frames it.
    void feed_mmu(const json& mmu) {
        json params;
        params["mmu"] = mmu;
        json notification;
        notification["params"] = json::array({params, 0.0});
        handle_status_update(notification);
    }
};

/// gate_status values: -1 unknown, 0 empty, 1 available, 2 from_buffer.
json gates(const std::vector<int>& gate_status, int gate, const char* filament) {
    return json{{"gate_status", gate_status}, {"gate", gate}, {"filament", filament}};
}

} // namespace

// ============================================================================
// The authority seam — Happy Hare deliberately does not claim it
// ============================================================================

TEST_CASE("Happy Hare keeps the aggregate load rule", "[ams][happy_hare][1199]") {
    HappyHarePerSlotLoadedHelper hh;
    CHECK_FALSE(hh.has_per_slot_loaded_authority());
}

// ============================================================================
// Hole 1: a from_buffer gate feeding the toolhead
// ============================================================================

TEST_CASE("Happy Hare marks a from_buffer gate LOADED when it is the loaded gate",
          "[ams][happy_hare][1199]") {
    HappyHarePerSlotLoadedHelper hh;

    // Gate 2 is spliced from the buffer (gate_status 2) AND is the gate Happy
    // Hare reports loaded. Pre-fix the `status == AVAILABLE` precondition on the
    // upgrade meant it stayed FROM_BUFFER forever.
    hh.feed_mmu(gates({1, 1, 2, 1}, 2, "Loaded"));

    REQUIRE(hh.get_system_info().current_slot == 2);
    REQUIRE(hh.is_filament_loaded());

    CHECK(hh.get_slot_info(2).status == SlotStatus::LOADED);
    CHECK(hh.get_slot_info(0).status == SlotStatus::AVAILABLE);
    CHECK(hh.get_slot_info(1).status == SlotStatus::AVAILABLE);
    CHECK(hh.get_slot_info(3).status == SlotStatus::AVAILABLE);
}

TEST_CASE("Happy Hare leaves an unloaded from_buffer gate as FROM_BUFFER",
          "[ams][happy_hare][1199]") {
    // The upgrade must be about the loaded gate, not about from_buffer gates in
    // general — otherwise every buffered gate would read as seated.
    HappyHarePerSlotLoadedHelper hh;
    hh.feed_mmu(gates({1, 1, 2, 1}, 0, "Loaded"));

    CHECK(hh.get_slot_info(0).status == SlotStatus::LOADED);
    CHECK(hh.get_slot_info(2).status == SlotStatus::FROM_BUFFER);
}

// ============================================================================
// Hole 2: gate / filament arrive without gate_status
// ============================================================================

TEST_CASE("Happy Hare moves the LOADED stamp on a gate-only delta", "[ams][happy_hare][1199]") {
    HappyHarePerSlotLoadedHelper hh;
    hh.feed_mmu(gates({1, 1, 1, 1}, 0, "Loaded"));
    REQUIRE(hh.get_slot_info(0).status == SlotStatus::LOADED);

    SECTION("a toolchange frame carrying only gate follows the firmware") {
        // The frame a real toolchange produces: gate_status did not change, so
        // Moonraker does not resend it. Pre-fix the whole upgrade lived inside
        // the gate_status branch, so gate 0 kept LOADED and gate 3 never got it.
        hh.feed_mmu(json{{"gate", 3}});

        REQUIRE(hh.get_system_info().current_slot == 3);
        CHECK(hh.get_slot_info(3).status == SlotStatus::LOADED);
        CHECK(hh.get_slot_info(0).status == SlotStatus::AVAILABLE);
    }

    SECTION("an unload frame carrying only filament clears it") {
        hh.feed_mmu(json{{"filament", "Unloaded"}});

        REQUIRE_FALSE(hh.is_filament_loaded());
        for (int i = 0; i < 4; ++i) {
            CHECK(hh.get_slot_info(i).status != SlotStatus::LOADED);
        }
    }

    SECTION("a later gate_status delta does not resurrect the old gate") {
        hh.feed_mmu(json{{"gate", 3}});
        hh.feed_mmu(json{{"gate_status", json::array({1, 0, 1, 1})}});

        CHECK(hh.get_slot_info(3).status == SlotStatus::LOADED);
        CHECK(hh.get_slot_info(0).status == SlotStatus::AVAILABLE);
        CHECK(hh.get_slot_info(1).status == SlotStatus::EMPTY);
    }
}

// ============================================================================
// Where the aggregate and the per-gate status legitimately disagree
// ============================================================================

TEST_CASE("Happy Hare does not paint an empty gate as loaded", "[ams][happy_hare][1199]") {
    // gate_status 0 after a runout is real information: the gate has nothing
    // left to feed, and load_filament()'s "slot not available" refusal keys on
    // it. Happy Hare can still report that gate as the loaded one, because the
    // filament it already fed is at the toolhead — the same shape as the AD5X
    // #995 runout. This disagreement is precisely why Happy Hare keeps the
    // aggregate rule: switching to the per-gate status would blank the
    // active-lane highlight here.
    HappyHarePerSlotLoadedHelper hh;
    hh.feed_mmu(gates({0, 1, 1, 1}, 0, "Loaded"));

    CHECK(hh.get_slot_info(0).status == SlotStatus::EMPTY);
    CHECK(hh.slot_is_actively_loaded(0));
    CHECK_FALSE(hh.slot_is_actively_loaded(1));
}

TEST_CASE("Happy Hare bypass and no-gate states stamp nothing", "[ams][happy_hare][1199]") {
    HappyHarePerSlotLoadedHelper hh;

    SECTION("bypass (gate -2) is not a gate index") {
        hh.feed_mmu(gates({1, 1, 1, 1}, -2, "Loaded"));
        for (int i = 0; i < 4; ++i) {
            CHECK(hh.get_slot_info(i).status != SlotStatus::LOADED);
            CHECK_FALSE(hh.slot_is_actively_loaded(i));
        }
    }

    SECTION("no gate selected (-1) is not a gate index") {
        hh.feed_mmu(gates({1, 1, 1, 1}, -1, "Loaded"));
        for (int i = 0; i < 4; ++i) {
            CHECK(hh.get_slot_info(i).status != SlotStatus::LOADED);
        }
    }

    SECTION("a gate index past the end is ignored, not a crash") {
        hh.feed_mmu(gates({1, 1, 1, 1}, 9, "Loaded"));
        for (int i = 0; i < 4; ++i) {
            CHECK(hh.get_slot_info(i).status != SlotStatus::LOADED);
        }
    }
}
