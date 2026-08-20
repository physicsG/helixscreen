// SPDX-License-Identifier: GPL-3.0-or-later

// The toolhead context menu's entry rule, tested as a pure function — no LVGL,
// no display, no backend. Tapping a nozzle on a PARALLEL (tool changer) canvas
// asks a carriage question ("which head is mounted") that the per-slot menu
// could not answer, so these four entries are the whole feature. The last
// section wires the real menu against a mock backend under LVGL, for the one
// rule that lives outside the pure model: it opens only on a toolchanger.

#include "ui_ams_toolhead_menu.h"
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "ams_backend_mock.h"
#include "ams_state.h"
#include "ams_types.h"

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

TEST_CASE("Toolhead menu offers nothing while a print owns the toolhead",
          "[ams][toolhead_menu][safety]") {
    // Select, Park, Load and Unload all move the carriage or the filament, and
    // the backend refuses every one of them mid-print. A menu of four buttons
    // that each answer with a refusal is worse than no menu.
    auto printing =
        toolhead_menu_model(/*tool_index=*/3, /*mounted_tool=*/3, /*supports_park=*/true,
                            /*slot_present=*/true, /*can_unload=*/true,
                            /*print_blocks_ops=*/true);
    CHECK(toolhead_menu_is_empty(printing));

    // The identical head is fully actionable once the print is not blocking —
    // notably including a PAUSED job, which is the runout-recovery workflow.
    auto idle = toolhead_menu_model(3, 3, true, true, true, /*print_blocks_ops=*/false);
    CHECK(idle.show_park);
    CHECK(idle.show_unload);
}

// =============================================================================
// A head fed from another unit has no Load of its own
//
// The nozzle menu was the one surface still offering it. On a multiACE U1 the
// per-slot menu already withdrew Load for the ACE-fed position (the command
// names a bay and only that bay can pick one), but tapping the same head's
// NOZZLE still offered it -- and because that head is the one the ACE keeps
// empty, it was often the only Load on screen.
// =============================================================================

TEST_CASE("An externally-fed toolhead offers Unload but not Load",
          "[ams][toolhead_menu][multiace]") {
    using helix::ui::toolhead_menu_model;

    SECTION("filament waiting in the source unit does not put Load on this head") {
        // slot_present (the ACE bay holds filament) + nothing at the head is
        // exactly the state that produced the stray Load.
        const auto stock = toolhead_menu_model(3, 3, false, /*slot_present=*/true,
                                               /*can_unload=*/false, false,
                                               /*source_is_external=*/false);
        CHECK(stock.show_load);

        const auto fed = toolhead_menu_model(3, 3, false, /*slot_present=*/true,
                                             /*can_unload=*/false, false,
                                             /*source_is_external=*/true);
        CHECK_FALSE(fed.show_load);
    }

    SECTION("Unload survives — it needs no bay") {
        // ACE_UNLOAD_HEAD takes only the head, so this half stays available.
        const auto fed = toolhead_menu_model(3, 3, false, /*slot_present=*/true,
                                             /*can_unload=*/true, false,
                                             /*source_is_external=*/true);
        CHECK(fed.show_unload);
        CHECK_FALSE(fed.show_load);
    }

    SECTION("it is a property of the position, not a transient state") {
        for (bool mounted : {false, true}) {
            for (bool present : {false, true}) {
                INFO("mounted=" << mounted << " present=" << present);
                const auto m = toolhead_menu_model(3, mounted ? 3 : 0, true, present,
                                                   /*can_unload=*/false, false,
                                                   /*source_is_external=*/true);
                CHECK_FALSE(m.show_load);
            }
        }
    }
}

// =============================================================================
// The canvas reports a VIRTUAL tool number; the backend calls take a SLOT
//
// ui_system_path_canvas.h documents the click argument as "the VIRTUAL tool
// number shown on the badge, not the physical column", and every backend call
// the menu makes is slot-indexed -- including can_unload_from_toolhead(), whose
// name says toolhead while its parameter is documented as a slot. They coincide
// on the U1 (slot N feeds tool N) and diverge on a toolchanger under ASSIGN_TOOL
// remapping, where a menu keyed on the wrong one acts on the wrong head.
// =============================================================================

TEST_CASE("toolhead_slot_for_tool resolves through the op-button rule",
          "[ams][toolhead_menu][tool_index]") {
    // The menu resolves a badge's tool number to a slot ONCE, at show time,
    // through resolve_op_button_slot() -- the same rule the filament panel's
    // Load/Unload buttons use -- so the two surfaces can never name different
    // slots for one tool. It used to reverse-scan mapped_tool privately, the
    // other encoding of the same relation, which ams_tool_map_sync.h records
    // shipping out of step twice.
    AmsSystemInfo info;
    info.units.emplace_back();
    auto& unit = info.units.back();
    unit.slot_count = 3;
    unit.first_slot_global_index = 0;
    unit.slots.resize(3);
    for (int i = 0; i < 3; ++i) {
        unit.slots[static_cast<size_t>(i)].global_index = i;
    }
    // A remapped machine: tool_to_slot_map is what the backend publishes.
    info.tool_to_slot_map = {1, 2, 0}; // T0 -> slot 1, T1 -> slot 2, T2 -> slot 0

    CHECK(helix::ui::toolhead_slot_for_tool(info, 2) == 0);
    CHECK(helix::ui::toolhead_slot_for_tool(info, 0) == 1);
    CHECK(helix::ui::toolhead_slot_for_tool(info, 1) == 2);

    // Identity machine (the U1 publishes no map): the two are the same number,
    // which is why the bug was invisible there.
    AmsSystemInfo u1;
    u1.units.emplace_back();
    auto& heads = u1.units.back();
    heads.slot_count = 4;
    heads.slots.resize(4);
    for (int i = 0; i < 4; ++i) {
        heads.slots[static_cast<size_t>(i)].global_index = i;
        heads.slots[static_cast<size_t>(i)].mapped_tool = i;
    }
    for (int i = 0; i < 4; ++i) {
        CHECK(helix::ui::toolhead_slot_for_tool(u1, i) == i);
    }

    // Beyond the map: a toolchanger lane IS its tool number, so fall back to
    // identity -- never to current_slot, which is the resolver's single-tool
    // answer and means "the loaded lane of a multi-lane AMS".
    u1.current_slot = 2;
    CHECK(helix::ui::toolhead_slot_for_tool(u1, 9) == 9);
    info.current_slot = 2;
    CHECK(helix::ui::toolhead_slot_for_tool(info, 7) == 7);
}

// ============================================================================
// The menu is a toolchanger surface -- wiring, with LVGL
// ============================================================================

namespace {
/// Reaches the protected backdrop hook so the test can tap "outside the card".
struct OpenToolheadMenu : helix::ui::AmsToolheadMenu {
    using AmsToolheadMenu::on_backdrop_clicked;
};
} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "The toolhead menu opens only on a PARALLEL backend",
                 "[ams][toolhead_menu][ui_integration]") {
    // Every entry is a carriage or per-head operation, and on a hub/selector
    // backend `mounted_tool` is a concept that does not exist (always -1) -- so
    // the rule offered "Select" for every head from a nozzle tap that had been
    // inert before the menu existed. On Happy Hare that is selector motion, on
    // an ACE a full filament feed, on CFS/AFC a guaranteed refusal.
    auto mock = std::make_unique<AmsBackendMock>(4);
    auto* backend = mock.get();
    REQUIRE(backend->start().success());
    AmsState::instance().set_backend(std::move(mock));
    AmsState::instance().init_subjects(true);
    AmsState::instance().sync_from_backend();
    helix::ui::UpdateQueue::instance().drain();

    lv_obj_t* anchor = lv_obj_create(test_screen());
    lv_obj_set_size(anchor, 200, 100);
    lv_obj_update_layout(anchor);
    const lv_point_t pt{50, 50};

    SECTION("a selector (LINEAR) backend gets no menu") {
        REQUIRE(backend->get_topology() != PathTopology::PARALLEL);
        OpenToolheadMenu menu;
        CHECK_FALSE(menu.show_at(test_screen(), anchor, pt, 0, backend));
        CHECK(helix::ui::ContextMenu::active() == nullptr);
    }

    SECTION("a toolchanger (PARALLEL) backend does") {
        backend->set_tool_changer_mode(true);
        AmsState::instance().sync_from_backend();
        helix::ui::UpdateQueue::instance().drain();
        REQUIRE(backend->get_topology() == PathTopology::PARALLEL);
        OpenToolheadMenu menu;
        int cancelled = 0;
        menu.set_action_callback([&](helix::ui::AmsToolheadMenu::ToolheadAction a, int, int) {
            if (a == helix::ui::AmsToolheadMenu::ToolheadAction::CANCELLED) {
                ++cancelled;
            }
        });
        // T0 with nothing mounted: Select is offered, so a menu comes up.
        CHECK(menu.show_at(test_screen(), anchor, pt, 0, backend));
        CHECK(helix::ui::ContextMenu::active() == &menu);
        // ...and a tap on the shared backdrop cancels it through the base hook.
        menu.on_backdrop_clicked();
        CHECK(cancelled == 1);
        CHECK(helix::ui::ContextMenu::active() == nullptr);
    }

    AmsState::instance().set_backend(nullptr);
    helix::ui::UpdateQueue::instance().drain();
}
