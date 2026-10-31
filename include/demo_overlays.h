// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>

namespace helix {

/**
 * @brief Bring up a demo overlay/modal populated with representative sample data.
 *
 * These are screens that cannot be reached by pure navigation in mock mode
 * because they only appear in response to a real printer event (a pre-print
 * filament check, a filament runout, an active print) or require configured
 * state (a lock-screen PIN). The remote-control `demo` command uses this so the
 * screenshot/automation pipeline can capture them with the real widget
 * lifecycle (init_subjects/create/on_activate), not an empty shell.
 *
 * Must be called on the LVGL/UI thread. Parent is the active screen.
 *
 * @param name One of: preflight-check, runout-modal, lock-screen,
 *             print-status, print-tune, ams-error-toast,
 *             type-mismatch-toast, type-mismatch-toast-model,
 *             type-mismatch-review, spaghetti-detection, bed-check.
 * @param error When non-null, receives why a recognized demo could not be shown
 *              (left empty for an unknown name).
 * @return true if the name was recognized and the overlay shown, false otherwise.
 */
bool show_demo_overlay(const std::string& name, std::string* error = nullptr);

/// Raises the printer-type mismatch warning the way a detection pass does. The
/// active printer session owns the prompter that does it and installs this; it is
/// empty while no session is up.
using TypeMismatchDemo = std::function<void(const std::string& detected, int confidence,
                                            const std::string& saved, bool open_review)>;
void set_type_mismatch_demo(TypeMismatchDemo demo);

} // namespace helix
