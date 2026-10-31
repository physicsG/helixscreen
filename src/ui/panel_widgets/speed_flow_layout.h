// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Pure layout decision for the Speed / Flow home widget.
//
// Free of LVGL so it is unit-testable without a display. SpeedFlowWidget measures
// the minimum box each rung draws in for the state being decided and feeds it here;
// this header picks the rung and the flags the XML binds.

#include "src/ui/panel_widgets/size_rung.h"

namespace helix {

struct SpeedFlowInput {
    int content_w = 0; ///< the box the content draws in, chrome already subtracted
    int content_h = 0;
    bool printing = false;       ///< print_active: the measured readings exist
    bool physical_units = false; ///< the "mm/s and mm3/s lead" setting
    /// Minimum box per rung for THIS state, with the percentages drawn. Printing adds
    /// the measured readings and the steppers, so the idle and printing needs differ
    /// and the caller measures whichever state it is deciding for.
    RungNeeds needs;
    /// The same boxes with the measured readings, units included, standing in for the
    /// percentages on the three small rungs. Only consulted while printing with
    /// physical units on.
    RungNeeds measured_needs;
};

/// The published verdict. `rung` is SizeRung's int; the flags are 0/1 subjects.
struct SpeedFlowVerdict {
    SizeRung rung = SizeRung::Glance;
    bool fits = false;
    /// The measured reading is drawn beside or under the percentage (Cell and up).
    bool show_measured = false;
    /// Below Cell there is no room for both, so with physical units on the measured
    /// reading takes the percentage's place, provided it fits the rung the percentage
    /// chose: a rung is never given up for it.
    bool value_is_measured = false;
    bool show_steppers = false; ///< Full while printing
};

[[nodiscard]] constexpr SpeedFlowVerdict decide_speed_flow_layout(const SpeedFlowInput& in) {
    SpeedFlowVerdict v;
    v.rung = choose_size_rung(in.content_w, in.content_h, in.needs);
    v.fits = holds_any_rung(in.content_w, in.content_h, in.needs);
    const bool has_room = static_cast<int>(v.rung) >= static_cast<int>(SizeRung::Cell);
    v.show_measured = in.printing && has_room;
    v.value_is_measured = in.printing && in.physical_units && !has_room && v.fits &&
                          rung_box_fits(in.content_w, in.content_h, in.measured_needs.at(v.rung));
    v.show_steppers = in.printing && v.rung == SizeRung::Full;
    return v;
}

} // namespace helix
