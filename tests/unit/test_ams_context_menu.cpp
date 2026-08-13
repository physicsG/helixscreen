// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_context_menu.h"

#include "ams_backend_mock.h"
#include "ams_types.h"
#include "filament_database.h"
#include "filament_op_slot_resolver.h"

#include <optional>
#include <string>
#include <vector>

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

    static bool decide_show_backup_row(const helix::printer::EndlessSpoolCapabilities& caps,
                                       bool has_relation) {
        return AmsContextMenu::decide_show_backup_row(caps, has_relation);
    }

    using BackupEligibleFn = AmsContextMenu::BackupEligibleFn;
    using SlotDisplayNumberFn = AmsContextMenu::SlotDisplayNumberFn;

    static std::string build_backup_options_for(int total_slots, int item_index,
                                                const BackupEligibleFn& eligible,
                                                const SlotDisplayNumberFn& display_number = {}) {
        return AmsContextMenu::build_backup_options_for(total_slots, item_index, eligible,
                                                        display_number);
    }

    static bool decide_backup_refused(int item_index, int backup_slot,
                                      const BackupEligibleFn& eligible) {
        return AmsContextMenu::decide_backup_refused(item_index, backup_slot, eligible);
    }
};

// The backup dropdown had no test at all, and CFS is what exposed the gap: it
// reports the feature as available and read-only but has no per-slot relation of
// any kind, so the row rendered with a permanently empty value.
TEST_CASE("AmsContextMenu::decide_show_backup_row needs a relation, not just availability",
          "[ams][context_menu][endless_spool]") {
    using namespace helix::printer;

    const EndlessSpoolCapabilities unsupported;

    EndlessSpoolCapabilities afc{.availability = EndlessSpoolAvailability::Available,
                                 .enabled = EndlessSpoolEnabled::On,
                                 .editability = EndlessSpoolEditability::PerSlot};

    EndlessSpoolCapabilities hh_multi_unit{.availability = EndlessSpoolAvailability::Available,
                                           .enabled = EndlessSpoolEnabled::On,
                                           .editability = EndlessSpoolEditability::ReadOnly,
                                           .restriction = EndlessSpoolRestriction::MultiUnit};

    EndlessSpoolCapabilities cfs{.availability = EndlessSpoolAvailability::Available,
                                 .enabled = EndlessSpoolEnabled::On,
                                 .editability = EndlessSpoolEditability::ReadOnly,
                                 .restriction = EndlessSpoolRestriction::FirmwareManaged};

    EndlessSpoolCapabilities ad5x_stock{.availability = EndlessSpoolAvailability::Available,
                                        .enabled = EndlessSpoolEnabled::On,
                                        .editability = EndlessSpoolEditability::ReadOnly,
                                        .restriction = EndlessSpoolRestriction::FirmwareManaged,
                                        .provider = "zmod"};

    // No real backend currently reports RequiresPlugin — AD5X stock zMod moved
    // to Available/FirmwareManaged once source read of ANALOG_PRUTOK landed.
    // Kept as a synthetic so the rendering path stays covered for any future
    // backend whose package genuinely can be missing.
    EndlessSpoolCapabilities synthetic_plugin_missing{
        .availability = EndlessSpoolAvailability::RequiresPlugin,
        .enabled = EndlessSpoolEnabled::Off,
        .editability = EndlessSpoolEditability::ReadOnly,
        .restriction = EndlessSpoolRestriction::PluginMissing};

    SECTION("no such feature: never") {
        CHECK_FALSE(AmsContextMenuTestAccess::decide_show_backup_row(unsupported, false));
        CHECK_FALSE(AmsContextMenuTestAccess::decide_show_backup_row(unsupported, true));
    }

    SECTION("plugin not installed: never - there is nothing to configure yet") {
        CHECK_FALSE(
            AmsContextMenuTestAccess::decide_show_backup_row(synthetic_plugin_missing, false));
        CHECK_FALSE(
            AmsContextMenuTestAccess::decide_show_backup_row(synthetic_plugin_missing, true));
    }

    SECTION("AD5X stock zMod: same shape as CFS, hides when no relation") {
        // FirmwareManaged + ReadOnly + no per-slot relation -> the CFS rule.
        CHECK_FALSE(AmsContextMenuTestAccess::decide_show_backup_row(ad5x_stock, false));
    }

    SECTION("editable: always, even before anything is configured") {
        CHECK(AmsContextMenuTestAccess::decide_show_backup_row(afc, false));
        CHECK(AmsContextMenuTestAccess::decide_show_backup_row(afc, true));
    }

    SECTION("read-only WITH a relation: shown, so the user can see it") {
        CHECK(AmsContextMenuTestAccess::decide_show_backup_row(hh_multi_unit, true));
    }

    SECTION("read-only with NO relation: hidden - this is the CFS fix") {
        // Showing it produced a dropdown stuck on "None" that could never be
        // told apart from "no backup configured".
        CHECK_FALSE(AmsContextMenuTestAccess::decide_show_backup_row(cfs, false));
    }
}

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

// =============================================================================
// Backup eligibility now flows through the BACKEND virtual
//
// build_backup_options() used to call filament::are_materials_compatible()
// directly, so AmsBackend::is_endless_spool_backup_eligible() had no production
// caller at all: AD5X IFS's stricter firmware rule could never reach the
// "(incompatible)" label, and no backend could tighten or loosen it.
// =============================================================================

TEST_CASE("Backup options are tagged by the backend's eligibility rule",
          "[ams][context_menu][endless_spool][1250]") {
    using Access = AmsContextMenuTestAccess;

    // The base-class default, replicated here so the "unchanged for AFC/HH"
    // claim is checked against the rule itself and not against a mock of it.
    // AmsBackend::is_endless_spool_backup_eligible() is this function over
    // get_slot_info(slot).material.
    const std::vector<std::string> materials = {"PLA", "PLA", "ABS", ""};
    const auto default_rule = [&materials](int slot, int candidate) {
        if (slot < 0 || candidate < 0 || slot == candidate) {
            return false;
        }
        const std::string& a = materials[static_cast<size_t>(slot)];
        const std::string& b = materials[static_cast<size_t>(candidate)];
        if (a.empty() || b.empty()) {
            return true;
        }
        return filament::are_materials_compatible(a, b);
    };

    SECTION("the current slot is skipped and None leads") {
        const auto opts = Access::build_backup_options_for(4, 1, default_rule);
        CHECK(opts.rfind("None", 0) == 0);
        // Slots 1, 3, 4 in 1-based labels — slot index 1 ("Slot 2") is absent.
        CHECK(opts.find("Slot 2") == std::string::npos);
        CHECK(opts.find("Slot 1") != std::string::npos);
        CHECK(opts.find("Slot 3") != std::string::npos);
        CHECK(opts.find("Slot 4") != std::string::npos);
    }

    SECTION("default rule: incompatible material is tagged, unknown material is not") {
        // Slot 0 is PLA. Slot 1 (PLA) compatible; slot 2 (ABS) not; slot 3
        // (unlabelled) counts as eligible rather than blocking a lane the user
        // simply has not filled in yet.
        const auto opts = Access::build_backup_options_for(4, 0, default_rule);
        const auto slot2 = opts.find("Slot 2");
        const auto slot3 = opts.find("Slot 3");
        const auto slot4 = opts.find("Slot 4");
        REQUIRE(slot2 != std::string::npos);
        REQUIRE(slot3 != std::string::npos);
        REQUIRE(slot4 != std::string::npos);

        CHECK(opts.substr(slot2, slot3 - slot2).find("(incompatible)") == std::string::npos);
        CHECK(opts.substr(slot3, slot4 - slot3).find("(incompatible)") != std::string::npos);
        CHECK(opts.substr(slot4).find("(incompatible)") == std::string::npos);
    }

    SECTION("a stricter backend rule reaches the label - the AD5X shape") {
        // Exact material AND exact colour AND port present. All four lanes hold
        // PLA, so the default material rule allows every pairing and tags
        // nothing; AD5X still refuses two of them. Before this change that
        // refusal had no way to reach the label at all.
        const std::vector<std::string> ifs_materials = {"PLA", "PLA", "PLA", "PLA"};
        const std::vector<std::string> colors = {"FF0000", "00FF00", "FF0000", "FF0000"};
        const std::vector<bool> present = {true, true, true, false};
        const auto strict = [&](int slot, int candidate) {
            if (slot < 0 || candidate < 0 || slot == candidate) {
                return false;
            }
            const auto s = static_cast<size_t>(slot);
            const auto c = static_cast<size_t>(candidate);
            if (ifs_materials[s].empty() || colors[s].empty()) {
                return false;
            }
            return present[c] && ifs_materials[c] == ifs_materials[s] && colors[c] == colors[s];
        };
        const auto lenient = [&ifs_materials](int slot, int candidate) {
            if (slot < 0 || candidate < 0 || slot == candidate) {
                return false;
            }
            return filament::are_materials_compatible(
                ifs_materials[static_cast<size_t>(slot)],
                ifs_materials[static_cast<size_t>(candidate)]);
        };

        const auto strict_opts = Access::build_backup_options_for(4, 0, strict);
        const auto lenient_opts = Access::build_backup_options_for(4, 0, lenient);
        CHECK(strict_opts != lenient_opts);
        // All PLA: the old rule had nothing to say about any of these lanes.
        CHECK(lenient_opts.find("(incompatible)") == std::string::npos);

        const auto s2 = strict_opts.find("Slot 2");
        const auto s3 = strict_opts.find("Slot 3");
        const auto s4 = strict_opts.find("Slot 4");
        REQUIRE(s2 != std::string::npos);
        REQUIRE(s3 != std::string::npos);
        REQUIRE(s4 != std::string::npos);
        // Slot index 1: same material, different colour -> refused.
        CHECK(strict_opts.substr(s2, s3 - s2).find("(incompatible)") != std::string::npos);
        // Slot index 2: same material, same colour, port present -> allowed.
        CHECK(strict_opts.substr(s3, s4 - s3).find("(incompatible)") == std::string::npos);
        // Slot index 3: material and colour match, but the port reads empty.
        CHECK(strict_opts.substr(s4).find("(incompatible)") != std::string::npos);
    }

    SECTION("no backend: nothing is tagged") {
        // Matches the old code, which skipped every check when backend_ was null.
        const auto always = [](int, int) { return true; };
        const auto opts = Access::build_backup_options_for(4, 0, always);
        CHECK(opts.find("(incompatible)") == std::string::npos);
    }

    SECTION("an out-of-menu item index tags nothing rather than indexing garbage") {
        bool called = false;
        const auto spy = [&called](int, int) {
            called = true;
            return false;
        };
        const auto opts = Access::build_backup_options_for(4, -1, spy);
        CHECK_FALSE(called);
        CHECK(opts.find("(incompatible)") == std::string::npos);
    }
}

TEST_CASE("The change handler refuses exactly what the option list tagged",
          "[ams][context_menu][endless_spool][1250]") {
    using Access = AmsContextMenuTestAccess;

    const auto refuse_all = [](int, int) { return false; };
    const auto allow_all = [](int, int) { return true; };

    SECTION("clearing a backup is always allowed") {
        // "None" needs nothing to be compatible with, so the rule is not even
        // consulted - a backend that refuses everything must not block a clear.
        CHECK_FALSE(Access::decide_backup_refused(0, -1, refuse_all));
    }

    SECTION("an ineligible pairing is refused") {
        CHECK(Access::decide_backup_refused(0, 2, refuse_all));
    }

    SECTION("an eligible pairing goes through") {
        CHECK_FALSE(Access::decide_backup_refused(0, 2, allow_all));
    }

    SECTION("no open slot: nothing to refuse") {
        CHECK_FALSE(Access::decide_backup_refused(-1, 2, refuse_all));
    }

    SECTION("label and refusal agree for every pairing under one rule") {
        // The invariant that matters: a slot tagged "(incompatible)" must be the
        // slot the handler bounces, for the same rule, with no third opinion.
        const std::vector<std::string> materials = {"PLA", "ABS", "PETG", "PLA"};
        const auto rule = [&materials](int slot, int candidate) {
            if (slot < 0 || candidate < 0 || slot == candidate) {
                return false;
            }
            return filament::are_materials_compatible(materials[static_cast<size_t>(slot)],
                                                      materials[static_cast<size_t>(candidate)]);
        };

        for (int item = 0; item < 4; ++item) {
            const auto opts = Access::build_backup_options_for(4, item, rule);
            for (int cand = 0; cand < 4; ++cand) {
                if (cand == item) {
                    continue;
                }
                const std::string label = "Slot " + std::to_string(cand + 1);
                const auto pos = opts.find(label);
                REQUIRE(pos != std::string::npos);
                const auto end = opts.find('\n', pos);
                const std::string entry = opts.substr(pos, end - pos);
                const bool tagged = entry.find("(incompatible)") != std::string::npos;
                CHECK(tagged == Access::decide_backup_refused(item, cand, rule));
            }
        }
    }
}

TEST_CASE("The option list is built from the live backend virtual, not a local rule",
          "[ams][context_menu][endless_spool][integration][1250]") {
    // A pure function is only as good as whether its inputs are the ones it gets
    // at runtime. Production binds `eligible` to
    // AmsBackend::is_endless_spool_backup_eligible() on the live backend
    // (AmsContextMenu::backend_eligible_fn()); this binds the same thing, so the
    // assertions below are about real slot data going through the real virtual.
    AmsBackendMock backend(4);
    backend.set_operation_delay(0);
    REQUIRE(backend.start());

    auto set_material = [&backend](int slot, const char* material) {
        SlotInfo info = backend.get_slot_info(slot);
        info.material = material;
        info.status = SlotStatus::AVAILABLE;
        REQUIRE(backend.set_slot_info(slot, info));
    };
    set_material(0, "PLA");
    set_material(1, "PLA");
    set_material(2, "ABS");
    set_material(3, "");

    const AmsContextMenuTestAccess::BackupEligibleFn live = [&backend](int slot, int candidate) {
        return backend.is_endless_spool_backup_eligible(slot, candidate);
    };

    SECTION("the virtual holds the values the call site actually passes") {
        // Guard against the pure function being right about inputs the backend
        // never produces: assert the backend's own view of the same slots.
        CHECK(backend.get_slot_info(0).material == "PLA");
        CHECK(backend.get_slot_info(2).material == "ABS");
        CHECK(backend.get_slot_info(3).material.empty());

        CHECK(live(0, 1));        // PLA / PLA
        CHECK_FALSE(live(0, 2));  // PLA / ABS
        CHECK(live(0, 3));        // PLA / unlabelled counts as eligible
        CHECK_FALSE(live(0, 0));  // a slot is never its own backup
        CHECK_FALSE(live(-1, 1)); // no open slot
    }

    SECTION("the default backend rule tags exactly the incompatible slot") {
        // "Unchanged for AFC/HH": the base virtual IS the old
        // are_materials_compatible() rule, so this option list is byte-identical
        // to the one the pre-Phase-2 code produced for the same slot data.
        const auto opts = AmsContextMenuTestAccess::build_backup_options_for(4, 0, live);
        const auto s2 = opts.find("Slot 2");
        const auto s3 = opts.find("Slot 3");
        const auto s4 = opts.find("Slot 4");
        REQUIRE(s2 != std::string::npos);
        REQUIRE(s3 != std::string::npos);
        REQUIRE(s4 != std::string::npos);

        CHECK(opts.substr(s2, s3 - s2).find("(incompatible)") == std::string::npos);
        CHECK(opts.substr(s3, s4 - s3).find("(incompatible)") != std::string::npos);
        CHECK(opts.substr(s4).find("(incompatible)") == std::string::npos);

        // And the handler refuses the same one.
        CHECK_FALSE(AmsContextMenuTestAccess::decide_backup_refused(0, 1, live));
        CHECK(AmsContextMenuTestAccess::decide_backup_refused(0, 2, live));
        CHECK_FALSE(AmsContextMenuTestAccess::decide_backup_refused(0, 3, live));
    }

    backend.stop();
}

// =============================================================================
// The backup list labels slots with the number their badge shows
//
// slot+1 is only right when every slot owns its identity. On multiACE an
// ACE-fed U1 head and the ACE bay behind it share one spool number, so the
// U1's four heads and one ACE span 1..7 rather than 1..8 — and a list offering
// "Slot 8" names nothing the user can point at.
// =============================================================================

TEST_CASE("Backup options use the badge's spool number, not slot + 1",
          "[ams][context_menu][endless_spool][multiace]") {
    using Access = AmsContextMenuTestAccess;
    const auto allow_all = [](int, int) { return true; };

    SECTION("the injected number is what appears in the list") {
        // The multiACE shape: head 3 is fed by ACE bay 0, so both read 4 and the
        // numbering continues 5, 6, 7 instead of running to 8.
        const std::vector<int> shown = {1, 2, 3, 4, 4, 5, 6, 7};
        const auto display = [&shown](int slot) { return shown[static_cast<size_t>(slot)]; };

        const auto opts = Access::build_backup_options_for(8, 0, allow_all, display);
        CHECK(opts.find("Slot 7") != std::string::npos);
        CHECK(opts.find("Slot 8") == std::string::npos); // the whole point
    }

    SECTION("no injected rule falls back to slot + 1") {
        // Every other backend numbers its slots exactly this way, and the
        // default argument is what keeps their lists byte-identical.
        const auto opts = Access::build_backup_options_for(4, 0, allow_all);
        CHECK(opts.find("Slot 4") != std::string::npos);
        CHECK(opts.find("Slot 5") == std::string::npos);
    }
}
