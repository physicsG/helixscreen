// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Pure layout decision for the Print Status widget's idle library actions.
//
// Free of LVGL so it is unit-testable without a display. PrintStatusWidget
// measures the action column, the token sizes and each visible button's
// labelled width, and publishes the verdict as two subjects the XML binds:
// labels hidden, and the column re-flowed into two rows.

#include "print_status_layout_decision.h"

namespace helix {

/// The library card offers at most this many action buttons.
inline constexpr int kLibraryActionsMax = 4;

struct LibraryActionsInput {
    int avail_w = 0;      ///< the action column's content width, which each stacked button fills
    int avail_h = 0;      ///< the action column's content height
    int gap = 0;          ///< gap between two stacked buttons
    int min_button_h = 0; ///< the shortest button still worth tapping (button_height_sm)
    int count = 0;        ///< visible buttons, at most kLibraryActionsMax
    /// Width each visible button needs to draw its label: side padding, the icon
    /// and its gap where the icon shows beside the label, and the label's text.
    int labeled_w[kLibraryActionsMax] = {};
};

struct LibraryActionsVerdict {
    bool icon_only = false; ///< every button drops its label together
    bool grid = false;      ///< two rows of buttons instead of one column
    /// In two rows, the visible button that starts the second; -1 in a column.
    int second_row_at = -1;
};

/// Stacking the visible buttons too short to tap (each under min_button_h)
/// drops every label, and with three or more buttons also re-flows them into two
/// rows: two on top, the rest below. One or two buttons already sit one per row.
/// Labels also go on every button at once when any one of them does not fit its
/// button's width. Nothing visible, or nothing measured yet, keeps the labelled
/// column.
[[nodiscard]] constexpr LibraryActionsVerdict
decide_library_actions_layout(const LibraryActionsInput& in) {
    LibraryActionsVerdict v;
    if (in.count <= 0 || in.count > kLibraryActionsMax || in.avail_w <= 0 || in.avail_h <= 0) {
        return v;
    }
    const int stacked_room = in.avail_h - (in.count - 1) * in.gap;
    const bool too_short = stacked_room < in.count * in.min_button_h;
    v.grid = too_short && in.count >= 3;
    v.second_row_at = v.grid ? 2 : -1;
    v.icon_only = too_short;
    for (int i = 0; i < in.count && !v.icon_only; ++i) {
        // One button alone in a row of its own width: the action-row rule.
        const int32_t needed = in.labeled_w[i];
        v.icon_only = !ui::action_row_fits(in.avail_w, 0, &needed, 1);
    }
    return v;
}

} // namespace helix
