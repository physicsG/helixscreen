// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_advanced_plugin_uninstall.cpp
 * @brief The Advanced panel's Uninstall HelixPrint Plugin row
 *
 * The row asks for confirmation and only then runs the uninstaller, which
 * removes the plugin's Moonraker component (helix_macros.cfg is kept) and
 * strips any old phase-tracking lines from PRINT_START. These cases pin that
 * the uninstaller runs on the confirm button and on nothing else (cancel,
 * backdrop dismissal), and that helix_plugin_installed clears on both
 * SUCCESS and NEEDS_ATTENTION - the plugin is gone either way - and only a
 * FAILED run leaves it alone.
 *
 * Both install and uninstall restart Moonraker over its API once the script
 * succeeds: the script runs under the service's NoNewPrivileges and cannot
 * sudo, so without that restart Moonraker never loads or drops the plugin.
 */

#include "ui_modal.h"
#include "ui_panel_advanced.h"
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/advanced_panel_test_access.h"
#include "app_globals.h"
#include "printer_state.h"

#include <string>

#include "../catch_amalgamated.hpp"

using helix::ui::AdvancedPanelTestAccess;
using helix::ui::UpdateQueue;

namespace {

struct PluginUninstallFixture : LVGLUITestFixture {
    using Outcome = helix::UninstallOutcome;

    AdvancedPanel panel{get_printer_state(), nullptr};

    int uninstall_runs = 0;
    int restarts = 0;
    Outcome uninstall_outcome = Outcome::SUCCESS;

    PluginUninstallFixture() {
        helix::ui::modal_init_subjects();

        // The recorder answers synchronously, the way the real installer does
        // once the script has exited.
        AdvancedPanelTestAccess::set_uninstall_runner(
            panel, [this](helix::HelixPluginInstaller::UninstallCallback done) {
                ++uninstall_runs;
                const char* message = uninstall_outcome == Outcome::SUCCESS ? "removed"
                                      : uninstall_outcome == Outcome::NEEDS_ATTENTION
                                          ? "removed, config needs a look"
                                          : "script failed";
                done(uninstall_outcome, message);
            });

        AdvancedPanelTestAccess::set_moonraker_restarter(panel, [this]() { ++restarts; });

        // The row is only reachable while the plugin is installed.
        get_printer_state().set_helix_plugin_installed(true);
        UpdateQueue::instance().drain();
        REQUIRE(plugin_installed() == 1);
    }

    ~PluginUninstallFixture() override {
        UpdateQueue::instance().drain();
    }

    int plugin_installed() {
        return lv_subject_get_int(
            get_printer_state().plugin_status_state().get_helix_plugin_installed_subject());
    }

    void tap_row() {
        AdvancedPanelTestAccess::tap_uninstall_row(panel);
        process_lvgl(50);
    }

    /// Answer the confirmation by clicking one of its buttons; the modal
    /// closes itself and the queued follow-ups need the drain.
    void answer_modal(const char* button_name) {
        lv_obj_t* dialog = ModalStack::instance().top_dialog();
        REQUIRE(dialog != nullptr);
        lv_obj_t* button = lv_obj_find_by_name(dialog, button_name);
        REQUIRE(button != nullptr);
        lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
        process_lvgl(50);
        UpdateQueue::instance().drain();
    }

    /// A backdrop tap: the dismissal path no button answers.
    void dismiss_modal() {
        lv_obj_t* dialog = ModalStack::instance().top_dialog();
        REQUIRE(dialog != nullptr);
        lv_obj_t* backdrop = ModalStack::instance().backdrop_for(dialog);
        REQUIRE(backdrop != nullptr);
        lv_obj_send_event(backdrop, LV_EVENT_CLICKED, nullptr);
        process_lvgl(50);
        UpdateQueue::instance().drain();
    }
};

} // namespace

TEST_CASE_METHOD(PluginUninstallFixture, "uninstall row asks before it runs anything",
                 "[advanced][plugin_installer][1232]") {
    tap_row();

    CHECK(uninstall_runs == 0);
    REQUIRE(ModalStack::instance().top_dialog() != nullptr);
    CHECK(plugin_installed() == 1);
}

TEST_CASE_METHOD(PluginUninstallFixture, "confirming the uninstall runs it and clears installed",
                 "[advanced][plugin_installer][1232]") {
    tap_row();
    answer_modal("btn_primary");

    CHECK(uninstall_runs == 1);
    CHECK(plugin_installed() == 0);
    CHECK(restarts == 1);
    CHECK(ModalStack::instance().stack_empty());
}

TEST_CASE_METHOD(PluginUninstallFixture, "cancelling the uninstall runs nothing",
                 "[advanced][plugin_installer][1232]") {
    tap_row();
    answer_modal("btn_secondary");

    CHECK(uninstall_runs == 0);
    CHECK(plugin_installed() == 1);
    CHECK(restarts == 0);
    CHECK(ModalStack::instance().stack_empty());
}

TEST_CASE_METHOD(PluginUninstallFixture, "dismissing the uninstall dialog runs nothing",
                 "[advanced][plugin_installer][1232]") {
    tap_row();
    dismiss_modal();

    CHECK(uninstall_runs == 0);
    CHECK(plugin_installed() == 1);
    CHECK(ModalStack::instance().stack_empty());
}

TEST_CASE_METHOD(PluginUninstallFixture, "a failed uninstall leaves the plugin marked installed",
                 "[advanced][plugin_installer][1232]") {
    uninstall_outcome = Outcome::FAILED;

    tap_row();
    answer_modal("btn_primary");

    CHECK(uninstall_runs == 1);
    CHECK(plugin_installed() == 1);
    CHECK(restarts == 0);
}

TEST_CASE_METHOD(PluginUninstallFixture, "a needs-attention uninstall still clears installed",
                 "[advanced][plugin_installer][1232]") {
    // The plugin's Moonraker component is gone either way; NEEDS_ATTENTION
    // only means a config file needs a human look, not that the plugin is
    // still there.
    uninstall_outcome = Outcome::NEEDS_ATTENTION;

    tap_row();
    answer_modal("btn_primary");

    CHECK(uninstall_runs == 1);
    CHECK(plugin_installed() == 0);
}

TEST_CASE_METHOD(PluginUninstallFixture,
                 "a successful install restarts Moonraker and leaves installed to discovery",
                 "[advanced][plugin_installer]") {
    get_printer_state().set_helix_plugin_installed(false);
    UpdateQueue::instance().drain();
    REQUIRE(plugin_installed() == 0);

    AdvancedPanelTestAccess::finish_install(panel, true);
    UpdateQueue::instance().drain();

    CHECK(restarts == 1);
    CHECK(plugin_installed() == 0);
}

TEST_CASE_METHOD(PluginUninstallFixture, "a failed install restarts nothing",
                 "[advanced][plugin_installer]") {
    get_printer_state().set_helix_plugin_installed(false);
    UpdateQueue::instance().drain();

    AdvancedPanelTestAccess::finish_install(panel, false);
    UpdateQueue::instance().drain();

    CHECK(restarts == 0);
    CHECK(plugin_installed() == 0);
}
