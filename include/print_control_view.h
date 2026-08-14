// include/print_control_view.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "printer_state.h" // for helix::PrintJobState enum

namespace helix::ui {

/// Optimistic in-flight action while a Pause/Resume RPC is unacknowledged.
/// Int values are the wire format of the `print_pending_action` subject.
enum class PendingAction : int { None = 0, Pausing = 1, Resuming = 2 };

/// MDI glyphs (UTF-8). pause=F03E4, play=F040A, hourglass=F051F.
inline constexpr const char* CONTROL_ICON_PAUSE = "\xF3\xB0\x8F\xA4";
inline constexpr const char* CONTROL_ICON_PLAY = "\xF3\xB0\x90\x8A";
inline constexpr const char* CONTROL_ICON_HOURGLASS = "\xF3\xB0\x94\x9F";

/// Pure view model for the two print-control buttons. No LVGL, no globals.
/// `primary_label` is an English string — callers pass it through lv_tr(),
/// which falls back to the string itself when no translation exists.
struct ControlButtonView {
    const char* primary_icon = CONTROL_ICON_PAUSE;
    const char* primary_label = "Pause";
    bool primary_enabled = false;
    bool stop_enabled = false;
};

ControlButtonView compute_control_button_view(helix::PrintJobState state, PendingAction pending,
                                              bool pause_available, bool resume_available,
                                              bool cancel_available);

} // namespace helix::ui
