// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Pure layout decision for the Toolhead home widget.
//
// Free of LVGL so it is unit-testable without a display. ToolheadWidget measures the
// minimum box each rung draws in and feeds it here; this header picks the rung and the
// flags the XML binds, and composes the axis value text the labels show.

#include "axis.h"
#include "src/ui/panel_widgets/size_rung.h"

#include <cstdio>
#include <string>

namespace helix {

struct ToolheadInput {
    int content_w = 0; ///< the box the content draws in, chrome already subtracted
    int content_h = 0;
    bool printing = false; ///< print_active: Glance and Strip swap the letters for Z height
    bool homed = false;    ///< all three axes report homed
    /// Minimum box per rung. Idle and printing draw into the same boxes at every
    /// rung, and Full is measured for the tallest footer (caption and Home All), so
    /// the rung does not change when homing finishes or a print starts.
    RungNeeds needs;
    /// Height the preset row adds below the Full rung's content. The row shows only when
    /// the box holds Full plus this much, so a 2x2 keeps its plate and a 2x3 gains the row.
    int presets_extra_h = 0;
    /// The Move-here chip with its label, and the gap Wide leaves between the plate and
    /// the values (the two share Wide's row equally). The chip drops its label on a plate
    /// box narrower than it; 0 leaves the label on.
    int labelled_chip_w = 0;
    int wide_gap_w = 0;
};

/// The published verdict. `rung` is SizeRung's int; the flags are 0/1 subjects.
struct ToolheadVerdict {
    SizeRung rung = SizeRung::Glance;
    bool fits = false;
    bool show_letters = false;     ///< Glance and Strip while idle: homed letters XYZ
    bool show_z_only = false;      ///< Glance and Strip while printing: the Z height
    bool show_rows = false;        ///< Column and Cell: one row per axis
    bool compact_values = false;   ///< Column: a half-cell-wide row has no room for decimals
    bool show_plate = false;       ///< Wide and up: plate, crosshair, Z bar, values
    bool show_home_button = false; ///< Full, idle, and an axis is not homed
    bool show_status_row = false;  ///< Full while printing: active tool and live speed
    bool show_presets = false;     ///< Full with presets_extra_h to spare, printing or not
    bool chip_icon_only = false;   ///< the plate is narrower than the labelled Move-here chip
};

/// The width of the box the plate draws in at @p rung: half the row at Wide, where the
/// values sit beside it, the whole width above.
[[nodiscard]] constexpr int toolhead_plate_box_w(const ToolheadInput& in, SizeRung rung) {
    return rung == SizeRung::Wide ? (in.content_w - in.wide_gap_w) / 2 : in.content_w;
}

[[nodiscard]] constexpr ToolheadVerdict decide_toolhead_layout(const ToolheadInput& in) {
    ToolheadVerdict v;
    v.rung = choose_size_rung(in.content_w, in.content_h, in.needs);
    v.fits = holds_any_rung(in.content_w, in.content_h, in.needs);
    const bool small = v.rung == SizeRung::Glance || v.rung == SizeRung::Strip;
    v.show_letters = small && !in.printing;
    v.show_z_only = small && in.printing;
    v.show_rows = v.rung == SizeRung::Column || v.rung == SizeRung::Cell;
    v.compact_values = v.rung == SizeRung::Column;
    v.show_plate = static_cast<int>(v.rung) >= static_cast<int>(SizeRung::Wide);
    v.show_home_button = v.rung == SizeRung::Full && !in.printing && !in.homed;
    v.show_status_row = v.rung == SizeRung::Full && in.printing;
    v.show_presets = v.rung == SizeRung::Full && in.presets_extra_h > 0 &&
                     in.content_h >= in.needs.full.h + in.presets_extra_h;
    v.chip_icon_only = v.show_plate && in.labelled_chip_w > 0 &&
                       toolhead_plate_box_w(in, v.rung) < in.labelled_chip_w;
    return v;
}

/// An axis position in millimetres, or "--" while the axis is not homed: an unhomed
/// axis reports a position that means nothing. Z reads two decimals, because layer
/// heights do; `compact` drops one decimal from Z and all of them from X and Y.
[[nodiscard]] inline std::string format_toolhead_axis(Axis axis, int centimm, bool homed,
                                                      bool compact = false) {
    if (!homed)
        return "--";
    const int decimals = axis == Axis::Z ? (compact ? 1 : 2) : (compact ? 0 : 1);
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, centimm / 100.0);
    return buf;
}

/// The position to show: where the nozzle physically is while the toolhead moves,
/// the toolhead's reported position otherwise.
[[nodiscard]] constexpr int toolhead_position_centimm(bool moving, int live_centimm,
                                                      int resting_centimm) {
    return moving ? live_centimm : resting_centimm;
}

} // namespace helix
