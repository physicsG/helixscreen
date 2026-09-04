// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Pure layout-decision logic shared by the home widgets that stack labelled
// rows: Temperatures (temp_stack) and Fan Speeds (fan_stack).
//
// Deliberately free of LVGL / widget state so it can be unit-tested without a
// display or font subsystem, the same shape as nozzle_layout.h. A widget
// measures its fonts and pixel height in on_size_changed() and feeds them
// here; this header decides whether each row draws as [icon | value] on one
// line or as an icon centred above its value.

namespace helix {

/// The rendered height of one row in the stacked (icon-above) layout.
struct StackedRowMetrics {
    int icon_px = 0;     ///< icon glyph line height
    int text_px = 0;     ///< value text line height
    int icon_gap_px = 0; ///< gap between the icon and its own value
};

/// Smallest authored rowspan, in half-cell grid tracks, that may stack.
///
/// The home grid lays out GridLayout::TRACKS_PER_CELL (2) tracks per cell and
/// both widgets set supports_half_row, so an authored height runs 2, 3 or 4
/// tracks — one cell, one and a half, or two. Stacking starts above one whole
/// cell. A static_assert at each call site ties this to the real
/// TRACKS_PER_CELL so a change to the grid cannot leave it stale.
inline constexpr int ICON_ABOVE_MIN_ROWSPAN = 3;

/// Decide whether the icon-above-temperature layout fits.
///
/// @param rowspan     authored grid tracks (half-cells) of height
/// @param avail_px    measured pixel height the widget was allocated
/// @param row_count   rows actually visible (chamber / aux fan may be hidden)
/// @param row         per-heater metrics in the stacked layout
/// @param row_gap_px  gap between two heaters
///
/// Both halves of the rule are load-bearing. The rowspan gate is what the
/// authored size means — a widget the user left at one cell keeps the compact
/// side-by-side rows however many pixels that cell is worth on a large panel.
/// The pixel test is the guard on the other side: a track is only ~34px on the
/// smallest panels, so three tracks there is not room for two lines per row
/// even though the span says "taller than a cell". Where the two disagree the
/// compact layout wins, which is also what panel_widget_size.h asks for.
///
/// A degenerate call — no pixels yet, nothing visible, or fonts that have not
/// resolved — returns false, the layout that fits in strictly less room.
[[nodiscard]] inline bool wants_icon_above(int rowspan, int avail_px, int row_count,
                                           const StackedRowMetrics& row, int row_gap_px) {
    if (rowspan < ICON_ABOVE_MIN_ROWSPAN)
        return false;
    if (avail_px <= 0 || row_count <= 0)
        return false;
    if (row.icon_px <= 0 || row.text_px <= 0)
        return false;

    const int per_row = row.icon_px + row.icon_gap_px + row.text_px;
    const int needed = row_count * per_row + (row_count - 1) * row_gap_px;
    return avail_px >= needed;
}

} // namespace helix
