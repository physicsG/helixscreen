// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_event.h"

#include <lvgl/lvgl.h> // lv_tr()

#include <string>

namespace helix::ui {

/// Title for a CRITICAL error modal (with or without recovery actions): the
/// event's own title, falling back to "Printer Error". A producer that knows
/// which subsystem raised the fault titles the event itself
/// (printer::klipper_error_classify does for its box-fault family).
///
/// Shared by GcodeErrorRouter (the plain CRITICAL arm) and
/// RecoveryModalPresenter (the MODAL_WITH_RECOVER arm) so the title rule
/// cannot drift between the two presentation paths.
inline const char* modal_title_for(const helix::ErrorEvent& e) {
    if (!e.title.empty()) {
        return e.title.c_str();
    }
    return lv_tr("Printer Error");
}

} // namespace helix::ui
