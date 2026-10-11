// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "quick_action_slots.h"

#include "../catch_amalgamated.hpp"

using helix::kQuickSlotLight;
using helix::QuickSlotInput;
using helix::QuickSlotKind;
using helix::resolve_quick_slots;

namespace {
QuickSlotInput unset(const char* def = "", bool renders = false) {
    return {false, def, renders};
}
QuickSlotInput set(std::string v, bool renders = false) {
    return {true, std::move(v), renders};
}
} // namespace

TEST_CASE("Quick slots: the light fills the first never-set empty slot", "[quick_slots]") {
    // Slots 1 and 2 default to macros this printer does not define.
    auto k =
        resolve_quick_slots({unset("clean_nozzle"), unset("bed_level"), unset(), unset()}, true);
    CHECK(k[0] == QuickSlotKind::Light);
    CHECK(k[1] == QuickSlotKind::Empty);
    CHECK(k[2] == QuickSlotKind::Empty);
}

TEST_CASE("Quick slots: a default that resolves keeps its macro", "[quick_slots]") {
    auto k = resolve_quick_slots(
        {unset("clean_nozzle", true), unset("bed_level", true), unset(), unset()}, true);
    CHECK(k[0] == QuickSlotKind::Macro);
    CHECK(k[1] == QuickSlotKind::Macro);
    CHECK(k[2] == QuickSlotKind::Light);
}

TEST_CASE("Quick slots: a slot the user cleared is never filled", "[quick_slots]") {
    auto k = resolve_quick_slots({set(""), set(""), set(""), set("")}, true);
    for (auto kind : k) {
        CHECK(kind == QuickSlotKind::Empty);
    }
}

TEST_CASE("Quick slots: four configured macros leave no room for the default", "[quick_slots]") {
    auto k = resolve_quick_slots({set("clean_nozzle", true), set("bed_level", true),
                                  set("purge", true), set("heat_soak", true)},
                                 true);
    for (auto kind : k) {
        CHECK(kind == QuickSlotKind::Macro);
    }
}

TEST_CASE("Quick slots: an assigned light shows once and suppresses the default", "[quick_slots]") {
    auto k = resolve_quick_slots(
        {unset("clean_nozzle"), unset("bed_level"), set(std::string(kQuickSlotLight)), unset()},
        true);
    CHECK(k[0] == QuickSlotKind::Empty);
    CHECK(k[2] == QuickSlotKind::Light);
    CHECK(k[3] == QuickSlotKind::Empty);
}

TEST_CASE("Quick slots: no controllable LED shows no light", "[quick_slots]") {
    auto k = resolve_quick_slots(
        {set(std::string(kQuickSlotLight)), unset("bed_level"), unset(), unset()}, false);
    for (auto kind : k) {
        CHECK(kind != QuickSlotKind::Light);
    }
}

TEST_CASE("Quick slots: a missing-but-assigned macro still occupies its slot", "[quick_slots]") {
    auto k = resolve_quick_slots({set("clean_nozzle", true), unset(), unset(), unset()}, true);
    CHECK(k[0] == QuickSlotKind::Macro);
    CHECK(k[1] == QuickSlotKind::Light);
}

TEST_CASE("Quick slot picker: shows the resolved value", "[quick_slots]") {
    const std::vector<std::string> names = {"load_filament", "clean_nozzle", "bed_level"};
    // A slot the light fills by default stores "" but shows Light (last entry).
    CHECK(helix::quick_slot_picker_index(QuickSlotKind::Light, "", names) == 4);
    // An assigned light shows Light too.
    CHECK(helix::quick_slot_picker_index(QuickSlotKind::Light, std::string(kQuickSlotLight),
                                         names) == 4);
    // A macro shows its own entry, and an empty slot "(Empty)".
    CHECK(helix::quick_slot_picker_index(QuickSlotKind::Macro, "clean_nozzle", names) == 2);
    CHECK(helix::quick_slot_picker_index(QuickSlotKind::Empty, "", names) == 0);
    // A stored slot this printer cannot run still shows what the user chose.
    CHECK(helix::quick_slot_picker_index(QuickSlotKind::Empty, "bed_level", names) == 3);
}

TEST_CASE("Quick slot defaults: two standard macros, then two empty slots", "[quick_slots]") {
    CHECK(std::string(helix::kQuickButtonDefaults[0]) == "clean_nozzle");
    CHECK(std::string(helix::kQuickButtonDefaults[1]) == "bed_level");
    CHECK(std::string(helix::kQuickButtonDefaults[2]).empty());
    CHECK(std::string(helix::kQuickButtonKeys[3]) == "/standard_macros/quick_button_4");
}
