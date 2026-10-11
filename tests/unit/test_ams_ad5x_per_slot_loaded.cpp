// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ams_ad5x_per_slot_loaded.cpp
 * @brief AD5X IFS per-slot load authority (#1199).
 *
 * update_slot_from_state() used to require the lane's own port sensor before
 * stamping SlotStatus::LOADED. On lessWaste/bambufy hardware — the variant that
 * actually has per-port sensors — a runout drops port_presence_ while the
 * filament it fed is still in the toolhead, so head_filament_ (and with it the
 * aggregate is_filament_loaded()) stays true while the slot fell to EMPTY. That
 * is the #995 state can_unload_from_toolhead() already keeps the unload gate
 * open for; the per-slot status disagreed with it, so believing the status
 * would have blanked the active-lane highlight during the exact runout the user
 * is recovering from.
 *
 * The seated lane is now LOADED whenever the head sensor sees filament,
 * whatever its port sensor reads, and the backend claims per-slot authority.
 *
 * Fixtures use the lessWaste save_variables shape (less_waste_tools 1:1 tool map
 * + less_waste_current_tool) so has_ifs_vars_ is set the way the plugin sets it,
 * and the port/head switches arrive under the stock filament_switch_sensor keys.
 */

#include "ams_backend_ad5x_ifs.h"
#include "ams_types.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using json = nlohmann::json;
using namespace helix;

namespace helix {

/// Feeds the production Moonraker status path (handle_status ->
/// parse_save_variables / parse_port_sensor / parse_head_sensor ->
/// recompute_current_slot_locked -> update_slot_from_state), so the parse chain
/// under test is the one the device drives.
class Ad5xPerSlotLoadedHelper : public AmsBackendAd5xIfs {
  public:
    Ad5xPerSlotLoadedHelper() : AmsBackendAd5xIfs(nullptr, nullptr) {
        // The _IFS_VARS macro-existence latch defaults to "missing" — only
        // on_started() clears it, against a live Moonraker — and
        // parse_save_variables refuses the plugin's tool map while it is set.
        // Clearing it here is what makes less_waste_tools land, mirroring
        // Ad5xIfsTestAccess::set_has_ifs_vars in the sibling suite.
        ifs_macro_confirmed_missing_ = false;
    }

    void feed(const json& status) {
        handle_status(status);
    }
};
} // namespace helix

namespace {

/// lessWaste's private save_variables namespace: a 1:1 tool->port map and the
/// active tool. Presence of less_waste_tools is what sets has_ifs_vars_, which
/// is what makes recompute_current_slot_locked() resolve current_slot through
/// the tool map rather than the native seated-channel path.
json less_waste_vars(int current_tool) {
    return json{{"less_waste_tools", json::array({1, 2, 3, 4})},
                {"less_waste_current_tool", current_tool}};
}

json save_variables(const json& vars) {
    return json{{"save_variables", json{{"variables", vars}}}};
}

json port_sensor(int port_1based, bool detected) {
    return json{{"filament_switch_sensor _ifs_port_sensor_" + std::to_string(port_1based),
                 json{{"filament_detected", detected}}}};
}

json head_sensor(bool detected) {
    return json{
        {"filament_switch_sensor head_switch_sensor", json{{"filament_detected", detected}}}};
}

/// Slot 0 seated and printing: T0 active, its port sensor sees the spool, the
/// head switch sees filament at the extruder.
void seat_slot0(Ad5xPerSlotLoadedHelper& ifs) {
    json frame = save_variables(less_waste_vars(0));
    frame.update(port_sensor(1, true));
    frame.update(port_sensor(3, true));
    frame.update(head_sensor(true));
    ifs.feed(frame);
}

} // namespace

// ============================================================================
// The authority seam
// ============================================================================

TEST_CASE("AD5X IFS claims per-slot load authority", "[ams][ad5x_ifs][1199]") {
    Ad5xPerSlotLoadedHelper ifs;
    CHECK(ifs.has_per_slot_loaded_authority());
}

// ============================================================================
// The parse contract the authority flag depends on
// ============================================================================

TEST_CASE("AD5X IFS stamps LOADED on the seated lane", "[ams][ad5x_ifs][1199]") {
    Ad5xPerSlotLoadedHelper ifs;
    seat_slot0(ifs);

    REQUIRE(ifs.get_system_info().current_slot == 0);
    REQUIRE(ifs.is_filament_loaded());

    SECTION("the seated lane is LOADED and every other lane is not") {
        CHECK(ifs.get_slot_info(0).status == SlotStatus::LOADED);
        CHECK(ifs.slot_is_actively_loaded(0));

        // Slot 2 has a spool but is not at the head — AVAILABLE, never LOADED.
        CHECK(ifs.get_slot_info(2).status == SlotStatus::AVAILABLE);
        CHECK_FALSE(ifs.slot_is_actively_loaded(2));
        // Slot 1 has no spool at all.
        CHECK(ifs.get_slot_info(1).status == SlotStatus::EMPTY);
        CHECK_FALSE(ifs.slot_is_actively_loaded(1));
        CHECK_FALSE(ifs.slot_is_actively_loaded(3));
    }

    SECTION("an empty head clears it even while the lane keeps its spool") {
        ifs.feed(head_sensor(false));

        REQUIRE_FALSE(ifs.is_filament_loaded());
        CHECK(ifs.get_slot_info(0).status == SlotStatus::AVAILABLE);
        CHECK_FALSE(ifs.slot_is_actively_loaded(0));
    }

    SECTION("out-of-range slots are false, not a crash") {
        CHECK_FALSE(ifs.slot_is_actively_loaded(-1));
        CHECK_FALSE(ifs.slot_is_actively_loaded(99));
    }
}

// ============================================================================
// The runout the per-slot rule must survive (#995)
// ============================================================================

TEST_CASE("AD5X IFS keeps the seated lane loaded through a port-sensor runout",
          "[ams][ad5x_ifs][1199][995]") {
    Ad5xPerSlotLoadedHelper ifs;
    seat_slot0(ifs);
    REQUIRE(ifs.get_slot_info(0).status == SlotStatus::LOADED);

    // Runout on hardware WITH per-port sensors: the spool leaves the lane
    // sensor, but the filament it already fed is still at the extruder, so the
    // head switch stays tripped and the firmware still names slot 0 active.
    ifs.feed(port_sensor(1, false));

    REQUIRE(ifs.get_system_info().current_slot == 0);
    REQUIRE(ifs.is_filament_loaded()); // the aggregate pair still says loaded

    // Pre-fix the port sensor was a precondition of the LOADED stamp, so this
    // fell to EMPTY — the per-slot rule would then have reported the lane
    // unloaded and blanked the highlight mid-recovery.
    CHECK(ifs.get_slot_info(0).status == SlotStatus::LOADED);
    CHECK(ifs.slot_is_actively_loaded(0));

    // The #995 unload gate and the highlight now agree instead of diverging.
    CHECK(ifs.can_unload_from_toolhead(0));

    // The runout does not spill onto any other lane.
    CHECK_FALSE(ifs.slot_is_actively_loaded(1));
    CHECK_FALSE(ifs.slot_is_actively_loaded(2));
}

// ============================================================================
// Hardware without per-port sensors is unaffected
// ============================================================================

TEST_CASE("AD5X IFS marks the seated lane loaded with no port sensors at all",
          "[ams][ad5x_ifs][1199]") {
    // The ZMOD-only variant publishes no _ifs_port_sensor_* objects, so
    // port_presence_ stays false for every lane. Before the change a special
    // case forced presence true for the active lane to keep it off EMPTY; the
    // rule now keys on the head sensor directly, which must produce the same
    // answer here.
    Ad5xPerSlotLoadedHelper ifs;

    json frame = save_variables(less_waste_vars(1)); // T1 -> port 2 -> slot 1
    frame.update(head_sensor(true));
    ifs.feed(frame);

    REQUIRE(ifs.get_system_info().current_slot == 1);
    CHECK(ifs.get_slot_info(1).status == SlotStatus::LOADED);
    CHECK(ifs.slot_is_actively_loaded(1));
    CHECK(ifs.get_slot_info(0).status == SlotStatus::EMPTY);
    CHECK_FALSE(ifs.slot_is_actively_loaded(0));
}

// ============================================================================
// A toolchange moves the stamp with the firmware's active-lane pointer
// ============================================================================

TEST_CASE("AD5X IFS moves the LOADED stamp on a toolchange", "[ams][ad5x_ifs][1199]") {
    Ad5xPerSlotLoadedHelper ifs;
    seat_slot0(ifs);
    REQUIRE(ifs.slot_is_actively_loaded(0));

    // T2 becomes active (-> port 3 -> slot 2); slot 2's spool is already there.
    ifs.feed(save_variables(less_waste_vars(2)));

    REQUIRE(ifs.get_system_info().current_slot == 2);
    CHECK(ifs.get_slot_info(2).status == SlotStatus::LOADED);
    CHECK(ifs.slot_is_actively_loaded(2));
    // The lane we left must not keep a stale LOADED — two highlighted lanes is
    // exactly the failure the per-slot rule cannot self-correct.
    CHECK(ifs.get_slot_info(0).status == SlotStatus::AVAILABLE);
    CHECK_FALSE(ifs.slot_is_actively_loaded(0));
}
