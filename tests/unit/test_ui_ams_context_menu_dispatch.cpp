// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ui_ams_context_menu_dispatch.cpp
 * @brief Regression tests for the shared AMS context-menu action dispatch
 *
 * AmsPanel and AmsOverviewPanel each wire their own AmsContextMenu action
 * callback. The two switches drifted: Overview handled LOAD/UNLOAD/EDIT/
 * SPOOLMAN/RECOVER_POSITION/SCAN_QR and ended in `case CANCELLED: default:
 * break;`, so EJECT, SELECT_GATE, CHECK_GATE and CLEAR_SPOOL fell into the
 * default arm and were discarded with no toast and no log line. Multi-unit
 * setups render the Overview, so every AFC user with two units (e.g. BoxTurtle
 * + NightOwl) had a dead Eject button (prestonbrown/helixscreen#1258).
 *
 * ams_dispatch_backend_action() now owns those five actions for both panels.
 * These tests pin the contract: the backend-only actions are claimed, and the
 * panel-specific ones are declined so each panel's own switch still sees them.
 */

#include "ui_ams_detail.h"

#include "../lvgl_ui_test_fixture.h"
#include "ams_backend_mock.h"
#include "ams_state.h"

#include "../catch_amalgamated.hpp"

using MenuAction = helix::ui::AmsContextMenu::MenuAction;

namespace {

/// Install a 4-slot mock backend so dispatch reaches real backend calls
/// rather than short-circuiting on the "no MFS available" guard.
void install_mock_backend() {
    AmsState::instance().init_subjects(false);
    auto mock = AmsBackend::create_mock(4);
    AmsState::instance().set_backend(std::move(mock));
    AmsState::instance().sync_from_backend();
}

} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "ams dispatch: claims every backend-only action",
                 "[ui][ams][context_menu][dispatch][1258]") {
    install_mock_backend();

    // These five must be handled centrally. If any one of them stops being
    // claimed here, the Overview panel silently swallows it again — which is
    // exactly the #1258 failure mode.
    const MenuAction shared[] = {MenuAction::EJECT, MenuAction::RECOVER_POSITION,
                                 MenuAction::SELECT_GATE, MenuAction::CHECK_GATE,
                                 MenuAction::CLEAR_SPOOL};

    for (MenuAction action : shared) {
        INFO("action index = " << static_cast<int>(action));
        CHECK(helix::ui::ams_dispatch_backend_action(action, 0, nullptr));
    }
}

TEST_CASE_METHOD(LVGLUITestFixture, "ams dispatch: declines panel-specific actions",
                 "[ui][ams][context_menu][dispatch][1258]") {
    install_mock_backend();

    // These open panel-owned modals or route through the panel's sidebar, so
    // they must fall through to the caller's own switch. Claiming one here
    // would make it a no-op in BOTH panels.
    const MenuAction panel_owned[] = {MenuAction::LOAD, MenuAction::UNLOAD, MenuAction::EDIT,
                                      MenuAction::SPOOLMAN, MenuAction::SCAN_QR};

    for (MenuAction action : panel_owned) {
        INFO("action index = " << static_cast<int>(action));
        CHECK_FALSE(helix::ui::ams_dispatch_backend_action(action, 0, nullptr));
    }

    // CANCELLED is a dismissal, not an operation — never claimed.
    CHECK_FALSE(helix::ui::ams_dispatch_backend_action(MenuAction::CANCELLED, 0, nullptr));
}

TEST_CASE_METHOD(LVGLUITestFixture, "ams dispatch: EJECT reaches the backend",
                 "[ui][ams][context_menu][dispatch][1258]") {
    install_mock_backend();

    auto* backend = static_cast<AmsBackendMock*>(AmsState::instance().get_backend());
    REQUIRE(backend != nullptr);

    SlotInfo info;
    info.slot_index = 1;
    info.material = "PLA";
    info.status = SlotStatus::AVAILABLE;
    backend->set_slot_info(1, info);
    AmsState::instance().sync_from_backend();

    // The whole point of #1258: the tap must actually arrive at the backend,
    // not be consumed by a switch that has no case for it.
    REQUIRE(helix::ui::ams_dispatch_backend_action(MenuAction::EJECT, 1, nullptr));
}

TEST_CASE_METHOD(LVGLUITestFixture, "ams dispatch: claims actions even with no backend",
                 "[ui][ams][context_menu][dispatch][1258]") {
    AmsState::instance().init_subjects(false);
    AmsState::instance().set_backend(nullptr);

    // With no MFS the user still gets a warning toast — the action is handled,
    // so the caller must not fall through and double-report it.
    CHECK(helix::ui::ams_dispatch_backend_action(MenuAction::EJECT, 0, nullptr));
    CHECK(helix::ui::ams_dispatch_backend_action(MenuAction::CLEAR_SPOOL, 0, nullptr));

    // Panel-specific actions are still declined regardless of backend state.
    CHECK_FALSE(helix::ui::ams_dispatch_backend_action(MenuAction::EDIT, 0, nullptr));
}
