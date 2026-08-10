// SPDX-License-Identifier: GPL-3.0-or-later

// The toolhead context menu's entry rule, tested as a pure function — no LVGL,
// no display, no backend. Tapping a nozzle on a PARALLEL (tool changer) canvas
// asks a carriage question ("which head is mounted") that the per-slot menu
// could not answer, so these four entries are the whole feature.

#include "ui_ams_toolhead_menu.h"

#include "../catch_amalgamated.hpp"

using helix::ui::toolhead_menu_is_empty;
using helix::ui::toolhead_menu_model;

TEST_CASE("Toolhead menu offers Select for a head that is not mounted", "[ams][toolhead_menu]") {
    // T1 tapped while T0 is on the carriage.
    auto m = toolhead_menu_model(/*tool_index=*/1, /*mounted_tool=*/0, /*supports_park=*/true,
                                 /*slot_present=*/false, /*can_unload=*/false);
    CHECK(m.show_select);
    // Park docks whatever is ON the carriage — it is not this head's action.
    CHECK_FALSE(m.show_park);
}

TEST_CASE("Toolhead menu offers Park only for the mounted head", "[ams][toolhead_menu]") {
    auto mounted = toolhead_menu_model(2, 2, true, false, false);
    CHECK(mounted.show_park);
    // Select would be a no-op on the head already mounted.
    CHECK_FALSE(mounted.show_select);

    SECTION("and never when the backend cannot park") {
        auto no_park = toolhead_menu_model(2, 2, /*supports_park=*/false, false, false);
        CHECK_FALSE(no_park.show_park);
        CHECK_FALSE(no_park.show_select);
        // Nothing left to offer at all — the caller must not show an empty card.
        CHECK(toolhead_menu_is_empty(no_park));
    }
}

TEST_CASE("Toolhead menu Load and Unload are mutually exclusive", "[ams][toolhead_menu]") {
    SECTION("filament at this toolhead offers Unload, never Load") {
        auto m = toolhead_menu_model(0, 0, true, /*slot_present=*/true, /*can_unload=*/true);
        CHECK(m.show_unload);
        CHECK_FALSE(m.show_load);
    }
    SECTION("a spool in the lane but not at the nozzle offers Load") {
        auto m = toolhead_menu_model(0, 0, true, /*slot_present=*/true, /*can_unload=*/false);
        CHECK(m.show_load);
        CHECK_FALSE(m.show_unload);
    }
    SECTION("an empty lane offers neither") {
        auto m = toolhead_menu_model(0, 0, true, /*slot_present=*/false, /*can_unload=*/false);
        CHECK_FALSE(m.show_load);
        CHECK_FALSE(m.show_unload);
    }
}

TEST_CASE("Toolhead menu: a parked head still offers its own filament actions",
          "[ams][toolhead_menu]") {
    // The per-tool unload fix exists precisely so a PARKED head holding filament
    // can be unloaded without mounting it first. The menu must not re-impose the
    // "mounted only" restriction that fix removed.
    auto m = toolhead_menu_model(/*tool_index=*/3, /*mounted_tool=*/0, true,
                                 /*slot_present=*/true, /*can_unload=*/true);
    CHECK(m.show_unload);
    CHECK(m.show_select);
    CHECK_FALSE(m.show_park);
}

TEST_CASE("Toolhead menu with no tool mounted still offers Select", "[ams][toolhead_menu]") {
    // Every head parked is a real state on the U1 (MountState::NONE), not an
    // absence of an answer — Select is exactly what the user needs there.
    auto m = toolhead_menu_model(0, /*mounted_tool=*/-1, true, false, false);
    CHECK(m.show_select);
    CHECK_FALSE(m.show_park);
}

TEST_CASE("Toolhead menu rejects an out-of-range head", "[ams][toolhead_menu]") {
    auto m = toolhead_menu_model(-1, 0, true, true, true);
    CHECK(toolhead_menu_is_empty(m));
}
