// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_batch_filament_modal.cpp
 * @brief Pure-logic tests for the batch filament picker modal
 *
 * The widget plumbing is LVGL-driven and covered by driving the mock; these
 * cases pin the two decisions that must hold regardless of the screen.
 */

#include "ui_batch_filament_modal.h"

#include "ams_backend_snapmaker.h"
#include "lvgl/src/others/translation/lv_translation.h"

#include <optional>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::ui::BatchFilamentModal;

namespace {
/// Every lane holds filament; only heads 0 and 2 are loaded at the toolhead.
/// This is the live rig state: four channels reporting filament_detected with
/// two at load_finish and two at preload_finish.
class DisagreeingBackend : public helix::AmsBackendSnapmaker {
  public:
    DisagreeingBackend() : helix::AmsBackendSnapmaker(nullptr, nullptr) {}

    helix::AmsSystemInfo get_system_info() const override {
        helix::AmsSystemInfo info;
        info.total_slots = 4;
        return info;
    }
    helix::SlotInfo get_slot_info(int slot_index) const override {
        helix::SlotInfo slot;
        slot.slot_index = slot_index;
        slot.status = helix::SlotStatus::AVAILABLE; // lane has filament
        return slot;
    }
    bool can_unload_from_toolhead(int slot_index) const override {
        return slot_index == 0 || slot_index == 2;
    }
};
} // namespace

TEST_CASE("BatchFilamentModal head_can_act folds both inputs per direction", "[ams][batch]") {
    using o = std::optional<bool>;

    SECTION("unload needs the filament at the toolhead, presence is irrelevant") {
        CHECK(BatchFilamentModal::head_can_act(o(true), o(false), false));
        CHECK(BatchFilamentModal::head_can_act(o(true), o(), false));
        CHECK_FALSE(BatchFilamentModal::head_can_act(o(false), o(true), false));
        CHECK_FALSE(BatchFilamentModal::head_can_act(o(), o(true), false));
    }
    SECTION("load needs a fed lane and an empty toolhead") {
        CHECK(BatchFilamentModal::head_can_act(o(false), o(true), true));
        CHECK(BatchFilamentModal::head_can_act(o(false), o(), true)); // unpublished: permissive
        CHECK_FALSE(BatchFilamentModal::head_can_act(o(false), o(false), true)); // empty lane
        CHECK_FALSE(BatchFilamentModal::head_can_act(o(true), o(true), true));   // already loaded
    }
}

TEST_CASE("BatchFilamentModal prefill ticks by direction", "[ams][batch]") {
    using o = std::optional<bool>;
    // head 0: loaded at its toolhead; head 1: empty lane; head 2: fed but not
    // loaded, presence unpublished; head 3: loaded at its toolhead.
    const std::vector<std::optional<bool>> at_toolhead{o(true), o(false), o(false), o(true)};
    const std::vector<std::optional<bool>> lane_presence{o(true), o(false), o(), o(true)};

    SECTION("unload ticks only heads with filament at the toolhead") {
        REQUIRE(BatchFilamentModal::prefill_selection(at_toolhead, lane_presence, false) ==
                std::vector<bool>{true, false, false, true});
    }
    SECTION("load ticks only fed lanes that are not at the toolhead") {
        // The empty lane (head 1) is NOT loadable: feeding it is the no-op the
        // firmware refuses. The unpublished presence (head 2) reads as fed.
        REQUIRE(BatchFilamentModal::prefill_selection(at_toolhead, lane_presence, true) ==
                std::vector<bool>{false, false, true, false});
    }
    SECTION("all feeders empty leaves Load with nothing to tick") {
        REQUIRE(BatchFilamentModal::prefill_selection(
                    std::vector<std::optional<bool>>{o(false), o(false)},
                    std::vector<std::optional<bool>>{o(false), o(false)},
                    true) == std::vector<bool>{false, false});
    }
    SECTION("no slots yields no ticks") {
        REQUIRE(BatchFilamentModal::prefill_selection({}, {}, true).empty());
        REQUIRE(BatchFilamentModal::prefill_selection({}, {}, false).empty());
    }
}

TEST_CASE("BatchFilamentModal any head answers the direction", "[ams][batch]") {
    using o = std::optional<bool>;
    const std::vector<std::optional<bool>> at_toolhead{o(true), o(false), o(false), o(true)};
    const std::vector<std::optional<bool>> lane_presence{o(true), o(false), o(), o(true)};

    SECTION("unload: a head with filament at the toolhead is enough") {
        REQUIRE(BatchFilamentModal::any_head_for_direction(at_toolhead, lane_presence, false));
    }
    SECTION("unload: no head at any toolhead is not") {
        REQUIRE_FALSE(BatchFilamentModal::any_head_for_direction(
            std::vector<std::optional<bool>>{o(false), o(false), o()},
            std::vector<std::optional<bool>>{o(true), o(true), o()}, false));
    }
    SECTION("load: a fed lane away from the toolhead is enough") {
        REQUIRE(BatchFilamentModal::any_head_for_direction(at_toolhead, lane_presence, true));
    }
    SECTION("load: every head already at the toolhead leaves nothing to load") {
        REQUIRE_FALSE(BatchFilamentModal::any_head_for_direction(
            std::vector<std::optional<bool>>{o(true), o(true)},
            std::vector<std::optional<bool>>{o(true), o(true)}, true));
    }
    SECTION("load: every feeder empty leaves nothing to load") {
        REQUIRE_FALSE(BatchFilamentModal::any_head_for_direction(
            std::vector<std::optional<bool>>{o(false), o(false)},
            std::vector<std::optional<bool>>{o(false), o(false)}, true));
    }
    SECTION("no slots answer neither direction") {
        REQUIRE_FALSE(BatchFilamentModal::any_head_for_direction({}, {}, true));
        REQUIRE_FALSE(BatchFilamentModal::any_head_for_direction({}, {}, false));
    }
}

TEST_CASE("BatchFilamentModal collects toolhead state separately from lane presence",
          "[ams][batch]") {
    DisagreeingBackend backend;

    const auto rows = BatchFilamentModal::collect_rows(backend);

    REQUIRE(rows.slots.size() == 4);
    CHECK(rows.lane_presence == std::vector<std::optional<bool>>{true, true, true, true});
    CHECK(rows.at_toolhead == std::vector<std::optional<bool>>{true, false, true, false});

    // The whole point: Unload pre-ticks the loaded heads, not every full lane.
    CHECK(BatchFilamentModal::prefill_selection(rows.at_toolhead, rows.lane_presence,
                                                /*for_load=*/false) ==
          std::vector<bool>{true, false, true, false});
}

TEST_CASE("BatchFilamentModal selected keys convert to slot indices", "[ams][batch]") {
    SECTION("keys in row order become slots") {
        REQUIRE(BatchFilamentModal::selected_slots({"0", "2", "3"}) == std::vector<int>{0, 2, 3});
    }
    SECTION("single slot") {
        REQUIRE(BatchFilamentModal::selected_slots({"1"}) == std::vector<int>{1});
    }
    SECTION("empty selection stays empty") {
        REQUIRE(BatchFilamentModal::selected_slots({}).empty());
    }
}

TEST_CASE("BatchFilamentModal row label names the lane contents", "[ams][batch]") {
    using helix::SlotInfo;
    using helix::ui::LaneNoun;

    SECTION("a lane feeding the toolhead says loaded") {
        SlotInfo info;
        info.material = "PETG";
        REQUIRE(BatchFilamentModal::row_label(LaneNoun::Feeder, 0, info, true, true) ==
                std::string("Feeder 1 (PETG - ") + lv_tr("loaded") + ")");
    }
    SECTION("a full lane not yet fed says ready to load") {
        SlotInfo info;
        info.material = "PLA";
        REQUIRE(BatchFilamentModal::row_label(LaneNoun::Feeder, 1, info, true, false) ==
                std::string("Feeder 2 (PLA - ") + lv_tr("ready to load") + ")");
    }
    SECTION("a lane known to be empty says so, with no loaded/ready suffix") {
        REQUIRE(BatchFilamentModal::row_label(LaneNoun::Feeder, 2, SlotInfo{}, false, false) ==
                std::string("Feeder 3 (") + lv_tr("Empty") + ")");
    }
    SECTION("an unanswerable presence leaves the lane name bare") {
        REQUIRE(BatchFilamentModal::row_label(LaneNoun::Feeder, 3, SlotInfo{}, std::nullopt,
                                              false) == "Feeder 4");
    }
    SECTION("material wins over presence, so a stale nullopt cannot blank a loaded lane") {
        SlotInfo info;
        info.material = "PLA";
        REQUIRE(BatchFilamentModal::row_label(LaneNoun::Slot, 0, info, std::nullopt, true) ==
                std::string("Slot 1 (PLA - ") + lv_tr("loaded") + ")");
    }
}

TEST_CASE("BatchFilamentModal sifts eligible slots and names the first refusal", "[ams][batch]") {
    using E = helix::AmsBackend::FilamentOpEligibility;
    const std::vector<E> per_slot = {E::Eligible, E::Empty, E::Eligible, E::Busy};

    SECTION("eligible slots keep their order; the first refusal is named") {
        const auto sift = BatchFilamentModal::sift_eligible({0, 1, 2, 3}, per_slot);
        CHECK(sift.eligible == std::vector<int>{0, 2});
        CHECK(sift.dropped == 1);
        CHECK(sift.drop_reason == E::Empty);
    }
    SECTION("nothing selected that is refused: nothing dropped") {
        const auto sift = BatchFilamentModal::sift_eligible({0, 2}, per_slot);
        CHECK(sift.eligible == std::vector<int>{0, 2});
        CHECK(sift.dropped == -1);
    }
    SECTION("a slot past the eligibility table is dropped without reading it") {
        // total_slots can shrink between the picker opening and the press.
        const auto sift = BatchFilamentModal::sift_eligible({0, 4, 5}, per_slot);
        CHECK(sift.eligible == std::vector<int>{0});
        CHECK(sift.dropped == -1); // no reason exists for an undescribable slot
    }
}
