// Copyright (C) 2025-2026 356C LLC
// tests/test_helpers/advanced_panel_test_access.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_panel_advanced.h"

#include <functional>
#include <utility>

namespace helix::ui {

// Test-only access to AdvancedPanel's HelixPrint plugin uninstall flow.
//
// The uninstall row's confirm step forks the bundled install.sh and blocks
// until it exits, so a test drives the ACTUAL row handler and swaps only the
// final step for a recorder: the dialog, its answers, and the state/toast
// follow-through stay the production code.
struct AdvancedPanelTestAccess {
    using UninstallRunner = AdvancedPanel::UninstallRunner;

    /// What a tap on row_helix_plugin_uninstall dispatches to.
    static void tap_uninstall_row(AdvancedPanel& panel) {
        panel.handle_helix_plugin_uninstall_clicked();
    }

    static void set_uninstall_runner(AdvancedPanel& panel, UninstallRunner runner) {
        panel.uninstall_runner_ = std::move(runner);
    }

    static void set_moonraker_restarter(AdvancedPanel& panel, std::function<void()> restarter) {
        panel.moonraker_restarter_ = std::move(restarter);
    }

    /// What the install modal reports once the script has exited.
    static void finish_install(AdvancedPanel& panel, bool success) {
        panel.on_helix_plugin_install_finished(success);
    }

    /// What taps on the helper-macro rows dispatch to.
    static void tap_macros_install_row(AdvancedPanel& panel) {
        panel.handle_helix_macros_install_clicked();
    }

    static void tap_macros_update_row(AdvancedPanel& panel) {
        panel.handle_helix_macros_update_clicked();
    }

    /// The panel wires this itself at init_subjects(); tests driving handlers
    /// without that path attach it explicitly.
    static void wire_macro_observer(AdvancedPanel& panel) {
        panel.wire_macro_restart_observer();
    }

    /// Whether the one-shot restart offer has already popped for this staging.
    static bool macro_restart_offer_made(const AdvancedPanel& panel) {
        return panel.macro_restart_offer_made_;
    }

    /// Whether setup() attached the print-active observer.
    static bool macro_observer_wired(const AdvancedPanel& panel) {
        return panel.macro_observer_wired_;
    }
};

} // namespace helix::ui
