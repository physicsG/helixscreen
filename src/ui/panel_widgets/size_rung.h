// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The size ladder every sized home widget shares, and the one chooser that walks it.
//
// Pure arithmetic, no LVGL: a widget measures what each rung would draw, hands the
// minimum box per rung to choose_size_rung(), and publishes the answer as a subject.
// The enum values ARE those subject ints, so XML ref_values and C++ cannot drift.

namespace helix {

enum class SizeRung : int {
    Glance = 0, ///< 0.5x0.5
    Strip = 1,  ///< 1x0.5, wide and short
    Column = 2, ///< 0.5x1, narrow and tall
    Cell = 3,   ///< 1x1
    Wide = 4,   ///< 2x1
    Full = 5,   ///< 2x2 and larger
    Large = 6,  ///< 3x3 and larger
};

/// The smallest content box one rung can draw in. A rung a widget does not offer
/// stays `offered = false` and is never chosen.
struct RungBox {
    int w = 0;
    int h = 0;
    bool offered = false;
};

constexpr RungBox rung_box(int w, int h) {
    return {w, h, true};
}

/// Per rung, the minimum box that rung's content needs in the state being decided.
struct RungNeeds {
    RungBox glance;
    RungBox strip;
    RungBox column;
    RungBox cell;
    RungBox wide;
    RungBox full;
    RungBox large;

    constexpr const RungBox& at(SizeRung rung) const {
        switch (rung) {
        case SizeRung::Glance:
            return glance;
        case SizeRung::Strip:
            return strip;
        case SizeRung::Column:
            return column;
        case SizeRung::Cell:
            return cell;
        case SizeRung::Wide:
            return wide;
        case SizeRung::Full:
            return full;
        case SizeRung::Large:
            return large;
        }
        return glance;
    }
};

constexpr bool rung_box_fits(int content_w, int content_h, const RungBox& box) {
    return box.offered && content_w >= box.w && content_h >= box.h;
}

/// The largest offered rung whose box fits on both axes, in the order Large, Full,
/// Wide, Cell, then Strip or Column, then Glance. A box that holds both Strip and
/// Column takes the one matching its aspect (wider than tall is Strip). A box that
/// holds nothing gets Glance; holds_any_rung() tells the two apart.
constexpr SizeRung choose_size_rung(int content_w, int content_h, const RungNeeds& needs) {
    if (rung_box_fits(content_w, content_h, needs.large))
        return SizeRung::Large;
    if (rung_box_fits(content_w, content_h, needs.full))
        return SizeRung::Full;
    if (rung_box_fits(content_w, content_h, needs.wide))
        return SizeRung::Wide;
    if (rung_box_fits(content_w, content_h, needs.cell))
        return SizeRung::Cell;
    const bool strip = rung_box_fits(content_w, content_h, needs.strip);
    const bool column = rung_box_fits(content_w, content_h, needs.column);
    if (strip && column)
        return content_w >= content_h ? SizeRung::Strip : SizeRung::Column;
    if (strip)
        return SizeRung::Strip;
    if (column)
        return SizeRung::Column;
    return SizeRung::Glance;
}

/// Whether the box holds at least one offered rung, which is what a widget's
/// fits_at() reports. Monotonic in both axes because every comparison is `>=`.
constexpr bool holds_any_rung(int content_w, int content_h, const RungNeeds& needs) {
    return rung_box_fits(content_w, content_h, needs.glance) ||
           rung_box_fits(content_w, content_h, needs.strip) ||
           rung_box_fits(content_w, content_h, needs.column) ||
           rung_box_fits(content_w, content_h, needs.cell) ||
           rung_box_fits(content_w, content_h, needs.wide) ||
           rung_box_fits(content_w, content_h, needs.full) ||
           rung_box_fits(content_w, content_h, needs.large);
}

} // namespace helix
