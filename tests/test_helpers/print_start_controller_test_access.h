// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file print_start_controller_test_access.h
 * @brief Friend shim for PrintStartController's private members.
 *
 * Declared `friend class ::PrintStartControllerTestAccess` in
 * ui_print_start_controller.h, so this class must live at GLOBAL scope and be
 * defined exactly once across the whole test binary — two translation units
 * each defining their own version with different members is an ODR violation
 * that links fine and misbehaves at runtime. Hence the shared header.
 */

#include "ui_print_start_controller.h"

#include <vector>

class PrintStartControllerTestAccess {
  public:
    // --- pre-print filament gate (test_print_start_filament_gate.cpp) ---

    static bool check(helix::ui::PrintStartController& c) {
        return c.check_required_filament_present();
    }

    static bool should_warn_remap_unsupported(const helix::printer::ToolMappingCapabilities& caps,
                                              bool applies_via_preprint) {
        return helix::ui::PrintStartController::should_warn_remap_unsupported(caps,
                                                                              applies_via_preprint);
    }

    // --- remap restore (test_remap_restore_confirmation.cpp) ---

    /// Seed the snapshot apply_filament_remaps() would have taken, without
    /// needing a real print-start round trip.
    static void seed_saved_mapping(helix::ui::PrintStartController& c, std::vector<int> mapping,
                                   int backend_index) {
        c.saved_tool_mapping_ = std::move(mapping);
        c.saved_backend_index_ = backend_index;
    }

    static const std::vector<int>& saved_mapping(const helix::ui::PrintStartController& c) {
        return c.saved_tool_mapping_;
    }

    static int saved_backend_index(const helix::ui::PrintStartController& c) {
        return c.saved_backend_index_;
    }

    static void restore(helix::ui::PrintStartController& c) {
        c.restore_filament_mapping();
    }
};
