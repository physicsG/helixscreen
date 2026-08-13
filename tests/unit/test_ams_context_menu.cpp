// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_context_menu.h"

#include "ams_types.h"
#include "filament_op_slot_resolver.h"

#include <optional>

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::ui;

// Forwards the private static predicates (friend access).
class AmsContextMenuTestAccess {
  public:
    static bool should_show_clear_spool(const SlotInfo& slot) {
        return AmsContextMenu::should_show_clear_spool(slot);
    }

    using UnloadMode = AmsContextMenu::UnloadMode;

    static UnloadMode decide_unload_mode(bool toolhead_unload, bool can_recover,
                                         bool recovery_attributed, bool supports_eject,
                                         bool slot_has_filament, bool supports_force_eject,
                                         bool slot_empty) {
        return AmsContextMenu::decide_unload_mode(toolhead_unload, can_recover, recovery_attributed,
                                                  supports_eject, slot_has_filament,
                                                  supports_force_eject, slot_empty);
    }

    // source_external defaults to false — the ordinary slot that holds its own
    // spool, which is what every case below except the ACE-fed ones is about.
    static bool decide_can_load(bool system_busy, bool toolhead_unload,
                                std::optional<bool> slot_has_filament, bool print_active,
                                bool source_external = false) {
        return AmsContextMenu::decide_can_load(system_busy, toolhead_unload, slot_has_filament,
                                               print_active, source_external);
    }

    static bool decide_unload_enabled(bool system_busy, UnloadMode mode, bool print_active,
                                      bool cold_ops_print_gated = false) {
        return AmsContextMenu::decide_unload_enabled(system_busy, mode, print_active,
                                                     cold_ops_print_gated);
    }
};

// "Clear Spool" was revealed only when `!slot_has_filament`, so it vanished the
// moment a new spool went into the lane — precisely when a stale assignment is
// most harmful, because that is when the wrong metadata gets printed with and
// when an edit will aim a Spoolman write at the previous spool.
//
// Stale metadata on an EMPTY lane is cosmetic. Stale metadata on a LOADED lane
// is the actual failure. Presence must not gate the affordance.
TEST_CASE("AmsContextMenu::should_show_clear_spool ignores whether filament is present",
          "[ams][context_menu]") {
    SECTION("assigned AND loaded still offers the clear — the regression") {
        SlotInfo slot;
        slot.status = SlotStatus::LOADED;
        slot.spoolman_id = 86;
        slot.material = "ASA";
        CHECK(AmsContextMenuTestAccess::should_show_clear_spool(slot));
    }

    SECTION("assigned and empty offers the clear") {
        SlotInfo slot;
        slot.status = SlotStatus::EMPTY;
        slot.spoolman_id = 86;
        slot.material = "ASA";
        CHECK(AmsContextMenuTestAccess::should_show_clear_spool(slot));
    }

    SECTION("material-only assignment counts, with no Spoolman link") {
        SlotInfo slot;
        slot.status = SlotStatus::LOADED;
        slot.spoolman_id = 0;
        slot.material = "PLA";
        CHECK(AmsContextMenuTestAccess::should_show_clear_spool(slot));
    }

    SECTION("spoolman-link-only assignment counts, with no material") {
        SlotInfo slot;
        slot.status = SlotStatus::AVAILABLE;
        slot.spoolman_id = 86;
        slot.material.clear();
        CHECK(AmsContextMenuTestAccess::should_show_clear_spool(slot));
    }

    SECTION("nothing assigned offers nothing to clear") {
        SlotInfo slot;
        slot.status = SlotStatus::AVAILABLE;
        slot.spoolman_id = 0;
        slot.material.clear();
        CHECK_FALSE(AmsContextMenuTestAccess::should_show_clear_spool(slot));
    }

    SECTION("empty and unassigned offers nothing to clear") {
        SlotInfo slot;
        slot.status = SlotStatus::EMPTY;
        slot.spoolman_id = 0;
        slot.material.clear();
        CHECK_FALSE(AmsContextMenuTestAccess::should_show_clear_spool(slot));
    }
}

// AmsContextMenu::decide_unload_mode() — Unload button operation selection.
//
// The BoxTurtle hub sensor is shared across every lane on a unit, so
// can_recover_lane_position() can read true for EVERY lane at once when AFC
// names no active lane. A prior version of this chain checked RecoverPosition
// unconditionally before Eject, which meant one unattributed stranded lane
// hid Eject from every seated lane sharing its hub — the bug this ruling
// fixes. lane_recovery_is_attributed() breaks the tie: attributed recovery
// outranks Eject, unattributed recovery defers to it.
using UnloadMode = AmsContextMenuTestAccess::UnloadMode;

TEST_CASE("AmsContextMenu::decide_unload_mode toolhead-loaded wins over everything",
          "[ams][context_menu]") {
    // Even if the backend also claims recovery is possible and attributed, a
    // slot that unloads via the heated toolhead path must take Unload.
    auto mode = AmsContextMenuTestAccess::decide_unload_mode(
        /*toolhead_unload=*/true, /*can_recover=*/true, /*recovery_attributed=*/true,
        /*supports_eject=*/true, /*slot_has_filament=*/true, /*supports_force_eject=*/true,
        /*slot_empty=*/false);
    CHECK(mode == UnloadMode::Unload);
}

TEST_CASE("AmsContextMenu::decide_unload_mode attributed strand outranks Eject",
          "[ams][context_menu]") {
    // AFC named this exact lane as active (lane_recovery_is_attributed==true).
    // Even though the lane also has filament present (would otherwise take
    // Eject), the confident diagnosis wins.
    auto mode = AmsContextMenuTestAccess::decide_unload_mode(
        /*toolhead_unload=*/false, /*can_recover=*/true, /*recovery_attributed=*/true,
        /*supports_eject=*/true, /*slot_has_filament=*/true, /*supports_force_eject=*/false,
        /*slot_empty=*/false);
    CHECK(mode == UnloadMode::RecoverPosition);
}

TEST_CASE(
    "AmsContextMenu::decide_unload_mode unattributed strand does not take Eject from a seated lane",
    "[ams][context_menu]") {
    // This is the regression the ruling fixes: an unattributed hub-wide trigger
    // (can_recover=true, recovery_attributed=false) must NOT preempt Eject on a
    // lane that is simply seated (slot_has_filament=true).
    auto mode = AmsContextMenuTestAccess::decide_unload_mode(
        /*toolhead_unload=*/false, /*can_recover=*/true, /*recovery_attributed=*/false,
        /*supports_eject=*/true, /*slot_has_filament=*/true, /*supports_force_eject=*/false,
        /*slot_empty=*/false);
    CHECK(mode == UnloadMode::Eject);
}

TEST_CASE("AmsContextMenu::decide_unload_mode unattributed lane with nothing ejectable still gets "
          "Recover",
          "[ams][context_menu]") {
    // No filament present to eject (slot_has_filament=false), so Eject is not an
    // option regardless of attribution — the unattributed Recover arm is the
    // last resort that still offers a way out for a lane with no other option.
    auto mode = AmsContextMenuTestAccess::decide_unload_mode(
        /*toolhead_unload=*/false, /*can_recover=*/true, /*recovery_attributed=*/false,
        /*supports_eject=*/true, /*slot_has_filament=*/false, /*supports_force_eject=*/false,
        /*slot_empty=*/true);
    CHECK(mode == UnloadMode::RecoverPosition);
}

TEST_CASE("AmsContextMenu::decide_unload_mode falls through to ForceEject and Unavailable",
          "[ams][context_menu]") {
    // No toolhead unload, no recovery possible at all, no eject support: an
    // empty lane with force-eject support gets ForceEject...
    auto force_eject = AmsContextMenuTestAccess::decide_unload_mode(
        /*toolhead_unload=*/false, /*can_recover=*/false, /*recovery_attributed=*/false,
        /*supports_eject=*/false, /*slot_has_filament=*/false, /*supports_force_eject=*/true,
        /*slot_empty=*/true);
    CHECK(force_eject == UnloadMode::ForceEject);

    // ...and with nothing at all supported, there is genuinely nothing to do.
    auto unavailable = AmsContextMenuTestAccess::decide_unload_mode(
        /*toolhead_unload=*/false, /*can_recover=*/false, /*recovery_attributed=*/false,
        /*supports_eject=*/false, /*slot_has_filament=*/false, /*supports_force_eject=*/false,
        /*slot_empty=*/true);
    CHECK(unavailable == UnloadMode::Unavailable);
}

// decide_can_load() must agree with AmsSubscriptionBackend::refuse_if_printing()
// in BOTH directions.
//
// Offering what the backend refuses is bundle JX2FVRB9: a runout-paused AD5X user
// tapped Load, following Klipper's own "load it and press RESUME" instruction,
// and got "Cannot run filament operation while printing".
//
// Refusing what the backend now ACCEPTS is the other half, and the reason this
// test changed shape: refuse_if_printing() no longer blocks a PAUSED print on a
// backend whose filament macro does not home itself. Pause-then-swap is the
// runout / colour-change recovery workflow on AFC, Happy Hare, CFS, ACE, QIDI,
// toolchangers and Snapmaker; only AD5X IFS still refuses it, because
// `_IFS_REMOVE_CURRENT_PRUTOK` runs a buried `_G28` that probes a loadcell-Z
// nozzle into the part (bundle XWPBR2DX).
//
// The parameter carries print_blocks_filament_op()'s answer, not the raw
// print_active subject — the tests below drive it through that predicate rather
// than hand-writing booleans, so a change to the rule shows up here.
//
// Mutation check: drop the `!print_blocks_op` term from decide_can_load() and
// "Load is refused while PRINTING" fails; make print_blocks_filament_op() ignore
// backend_self_homes and both PAUSED sections fail.
TEST_CASE("AmsContextMenu::decide_can_load agrees with the backend print guard",
          "[ams][context_menu][print_guard]") {
    using helix::ui::print_blocks_filament_op;

    auto can_load = [](bool printing, bool paused, bool self_homes) {
        return AmsContextMenuTestAccess::decide_can_load(
            /*system_busy=*/false, /*toolhead_unload=*/false, /*slot_has_filament=*/true,
            print_blocks_filament_op(printing, paused, self_homes));
    };

    SECTION("Load is offered for a filled, non-seated lane when no print is running") {
        CHECK(can_load(/*printing=*/false, /*paused=*/false, /*self_homes=*/false));
        CHECK(can_load(/*printing=*/false, /*paused=*/false, /*self_homes=*/true));
    }

    SECTION("Load is refused while PRINTING, on every backend") {
        CHECK_FALSE(can_load(/*printing=*/true, /*paused=*/false, /*self_homes=*/false));
        CHECK_FALSE(can_load(/*printing=*/true, /*paused=*/false, /*self_homes=*/true));
    }

    SECTION("Load is OFFERED on a paused print when the backend does not self-home") {
        // AFC / Happy Hare / CFS / ACE / QIDI / toolchanger / Snapmaker. This is
        // the recovery Klipper asks for; greying it made HelixScreen the only
        // surface that could not perform it.
        CHECK(can_load(/*printing=*/false, /*paused=*/true, /*self_homes=*/false));
    }

    SECTION("Load is still refused on a paused print when the backend self-homes") {
        // AD5X IFS only.
        CHECK_FALSE(can_load(/*printing=*/false, /*paused=*/true, /*self_homes=*/true));
    }

    SECTION("The pre-existing terms still hold") {
        CHECK_FALSE(AmsContextMenuTestAccess::decide_can_load(true, false, true, false)); // busy
        CHECK_FALSE(AmsContextMenuTestAccess::decide_can_load(false, true, true, false)); // seated
        CHECK_FALSE(AmsContextMenuTestAccess::decide_can_load(false, false, false, false)); // empty
    }

    SECTION("An UNKNOWN-presence lane is not treated as empty") {
        // SlotStatus::UNKNOWN means the backend publishes no per-lane presence,
        // not "the lane is empty" — slot_presence() reports it unanswerable and
        // Load stays reachable so the backend gets to refuse if it really is.
        CHECK(AmsContextMenuTestAccess::decide_can_load(false, false, std::nullopt, false));
    }
}

// Only the heated toolhead unload is subject to the print gate at all. The cold
// lane ops leave the toolhead parked where the print left it and the backend
// permits them via check_preconditions(false) — which never consults print state
// — so blocking the whole button would strand filament a paused user could
// legitimately eject.
//
// The print term itself is print_blocks_filament_op()'s answer, so PAUSED now
// also stops blocking the heated Unload on every backend but AD5X. That is the
// live Discord report: "Unload failed: Cannot run filament operation while
// printing", raised while merely PAUSED.
//
// Mutation check: delete the cold-lane arm and "Cold lane ops stay available"
// fails; make print_blocks_filament_op() ignore backend_self_homes and the
// paused sections fail.
TEST_CASE("AmsContextMenu::decide_unload_enabled blocks only the toolhead unload mid-print",
          "[ams][context_menu][print_guard]") {
    using helix::ui::print_blocks_filament_op;

    auto unload_enabled = [](UnloadMode mode, bool printing, bool paused, bool self_homes,
                             bool cold_ops_print_gated = false) {
        return AmsContextMenuTestAccess::decide_unload_enabled(
            /*system_busy=*/false, mode, print_blocks_filament_op(printing, paused, self_homes),
            cold_ops_print_gated);
    };

    SECTION("Toolhead unload is refused while PRINTING, on every backend") {
        CHECK(unload_enabled(UnloadMode::Unload, false, false, false));
        CHECK_FALSE(unload_enabled(UnloadMode::Unload, /*printing=*/true, false, false));
        CHECK_FALSE(unload_enabled(UnloadMode::Unload, /*printing=*/true, false, true));
    }

    SECTION("Toolhead unload is OFFERED on a paused print unless the backend self-homes") {
        CHECK(unload_enabled(UnloadMode::Unload, false, /*paused=*/true, /*self_homes=*/false));
        CHECK_FALSE(
            unload_enabled(UnloadMode::Unload, false, /*paused=*/true, /*self_homes=*/true));
    }

    SECTION("Cold lane ops stay available mid-print, even on a self-homing backend") {
        for (auto mode : {UnloadMode::Eject, UnloadMode::RecoverPosition, UnloadMode::ForceEject}) {
            CHECK(unload_enabled(mode, /*printing=*/true, false, /*self_homes=*/false));
            CHECK(unload_enabled(mode, /*printing=*/true, false, /*self_homes=*/true));
        }
    }

    // AFC's cmd_LANE_UNLOAD opens with its own is_printing() check, so the cold
    // exemption above would offer a button the firmware discards without moving
    // anything. The exemption is withdrawn per backend, not globally: the section
    // above must keep passing for everyone else.
    SECTION("Cold lane ops grey out while PRINTING when the firmware refuses them too") {
        for (auto mode : {UnloadMode::Eject, UnloadMode::RecoverPosition, UnloadMode::ForceEject}) {
            CHECK_FALSE(unload_enabled(mode, /*printing=*/true, false, /*self_homes=*/false,
                                       /*cold_ops_print_gated=*/true));
        }
    }

    // AFC's is_printing() is `state == "printing"` exactly, so a PAUSED job still
    // reaches the firmware. Greying it would break the pause-then-clear-a-strand
    // recovery this whole gate was narrowed to preserve.
    SECTION("A gated backend still offers cold lane ops while PAUSED") {
        for (auto mode : {UnloadMode::Eject, UnloadMode::RecoverPosition, UnloadMode::ForceEject}) {
            CHECK(unload_enabled(mode, false, /*paused=*/true, /*self_homes=*/false,
                                 /*cold_ops_print_gated=*/true));
        }
    }

    SECTION("Busy and Unavailable still win over everything") {
        CHECK_FALSE(
            AmsContextMenuTestAccess::decide_unload_enabled(true, UnloadMode::Eject, false));
        CHECK_FALSE(
            AmsContextMenuTestAccess::decide_unload_enabled(false, UnloadMode::Unavailable, false));
    }
}

// ============================================================================
// A position fed from another unit has no Load of its own
//
// An ACE-fed U1 head is loaded with `ACE_LOAD_HEAD HEAD=n ACE=a SLOT=s`, which
// names a specific bay — so the choice belongs to the bay's menu, not the
// head's. Unload needs no bay (`ACE_UNLOAD_HEAD HEAD=n`) and stays available,
// which is the asymmetry these cases pin.
// ============================================================================

TEST_CASE("AmsContextMenu::decide_can_load withdraws Load for a slot fed from another unit",
          "[ams][context_menu][source_external]") {
    SECTION("otherwise-loadable slot loses Load when it is externally fed") {
        // Idle, not seated, filament available: Load would be offered on any
        // ordinary slot.
        REQUIRE(AmsContextMenuTestAccess::decide_can_load(false, false, true, false,
                                                          /*source_external=*/false));
        CHECK_FALSE(AmsContextMenuTestAccess::decide_can_load(false, false, true, false,
                                                              /*source_external=*/true));
    }

    SECTION("it is a property of the position, not a transient state") {
        // Every combination stays false — this is "no such action here", not
        // "not right now", so no amount of idling re-enables it.
        for (bool busy : {false, true}) {
            for (bool seated : {false, true}) {
                for (bool printing : {false, true}) {
                    INFO("busy=" << busy << " seated=" << seated << " printing=" << printing);
                    CHECK_FALSE(AmsContextMenuTestAccess::decide_can_load(busy, seated, true,
                                                                          printing,
                                                                          /*external=*/true));
                }
            }
        }
    }

    SECTION("an ordinary slot is untouched") {
        // The default-argument path every other case in this file exercises.
        CHECK(AmsContextMenuTestAccess::decide_can_load(false, false, true, false));
        CHECK_FALSE(AmsContextMenuTestAccess::decide_can_load(true, false, true, false));
    }
}
