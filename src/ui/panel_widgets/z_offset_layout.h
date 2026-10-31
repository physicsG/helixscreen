// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Pure layout decision for the Live Z-offset home widget.
//
// Free of LVGL so it is unit-testable without a display. ZOffsetWidget measures the
// minimum box each rung draws in and feeds it here; this header picks the rung and the
// flags the XML binds, and composes the value text the labels show.

#include "src/ui/panel_widgets/size_rung.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

namespace helix {

struct ZOffsetInput {
    int content_w = 0; ///< the box the content draws in, chrome already subtracted
    int content_h = 0;
    /// A tool changer: the active tool's name takes a line at Full, so the caller
    /// measured the Full box with it.
    bool tool_changer = false;
    /// Minimum box per rung. Idle and printing draw the same content, so one table.
    RungNeeds needs;
};

/// The published verdict. `rung` is SizeRung's int; the flags are 0/1 subjects.
struct ZOffsetVerdict {
    SizeRung rung = SizeRung::Glance;
    bool fits = false;
    int decimals = 2;             ///< 2 at Glance, 3 on every larger rung
    bool show_save_row = false;   ///< Wide and up; the button still needs a save to offer
    bool show_steppers = false;   ///< Full
    bool show_tool_label = false; ///< Full on a tool changer
};

[[nodiscard]] constexpr ZOffsetVerdict decide_z_offset_layout(const ZOffsetInput& in) {
    ZOffsetVerdict v;
    v.rung = choose_size_rung(in.content_w, in.content_h, in.needs);
    v.fits = holds_any_rung(in.content_w, in.content_h, in.needs);
    v.decimals = v.rung == SizeRung::Glance ? 2 : 3;
    v.show_save_row = static_cast<int>(v.rung) >= static_cast<int>(SizeRung::Wide);
    v.show_steppers = v.rung == SizeRung::Full;
    v.show_tool_label = v.rung == SizeRung::Full && in.tool_changer;
    return v;
}

/// The offset in millimetres at `decimals` places, or "--" when nothing has been
/// reported. A value that rounds to zero draws without a sign.
[[nodiscard]] inline std::string format_z_offset_value(std::optional<int> microns, int decimals) {
    if (!microns)
        return "--";
    const double mm = static_cast<double>(*microns) / 1000.0;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, mm);
    const double rounded = std::strtod(buf, nullptr);
    if (rounded == 0.0)
        std::snprintf(buf, sizeof(buf), "%.*f", decimals, 0.0);
    return buf;
}

} // namespace helix
