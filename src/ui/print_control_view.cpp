// src/ui/print_control_view.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "print_control_view.h"

namespace helix::ui {

ControlButtonView compute_control_button_view(helix::PrintJobState state, PendingAction pending,
                                              bool pause_available, bool resume_available,
                                              bool cancel_available) {
    ControlButtonView v;

    const bool active =
        (state == helix::PrintJobState::PRINTING || state == helix::PrintJobState::PAUSED);
    const bool slot_ok =
        (state == helix::PrintJobState::PAUSED) ? resume_available : pause_available;

    v.primary_enabled = active && pending == PendingAction::None && slot_ok;
    v.stop_enabled = active && cancel_available;

    switch (pending) {
    case PendingAction::Pausing:
        v.primary_icon = CONTROL_ICON_HOURGLASS;
        v.primary_label = "Pausing...";
        break;
    case PendingAction::Resuming:
        v.primary_icon = CONTROL_ICON_HOURGLASS;
        v.primary_label = "Resuming...";
        break;
    case PendingAction::None:
        if (state == helix::PrintJobState::PAUSED) {
            v.primary_icon = CONTROL_ICON_PLAY;
            v.primary_label = "Resume";
        } else {
            v.primary_icon = CONTROL_ICON_PAUSE;
            v.primary_label = "Pause";
        }
        break;
    }
    return v;
}

} // namespace helix::ui
