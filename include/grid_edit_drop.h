// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "grid_layout.h"

#include <string>

/**
 * @file grid_edit_drop.h
 * @brief What a released home-grid edit drag does: move its widget, create a
 * page before the first or past the last one, swap it with the widget it lands
 * on, go back to its origin page, or snap back.
 *
 * Free of live objects: GridEditMode gathers the release into a DropInput and
 * the scoped page's occupancy, and commits what resolve_drop() returns
 * (prestonbrown/helixscreen#1638).
 */

namespace helix {

/// A released drag, in the terms its drop resolves on.
struct DropInput {
    int page_index = 0;  ///< Page the session is scoped to; page_count is the next-page slot
    int page_count = 0;  ///< Pages in the config
    int origin_page = 0; ///< Page the dragged entry lives on
    int origin_col = 0;  ///< The entry's cell on its origin page
    int origin_row = 0;
    int colspan = 1; ///< The entry's span
    int rowspan = 1;
    int target_col = -1; ///< Cell the drag previewed on the scoped page; -1 when none
    int target_row = -1;
    bool has_next_page_slot = false; ///< A page past the last one can be created
    bool past_right_border = false; ///< The widget's majority lies past the page frame's right edge
    bool past_left_border = false;  ///< The widget's majority lies past the page frame's left edge
};

enum class DropOutcome {
    Move,       ///< The entry lands on the scoped page at the resolved cell
    CreatePage, ///< A page is added, and the entry lands on it
    Swap, ///< The entry lands on the scoped page at the resolved cell, and the widget there moves
          ///< to the entry's origin cell
    ReturnToOrigin, ///< Nothing commits, and the drag goes back to its origin page first
    Cancel,         ///< Nothing commits, on the origin page: the widget snaps back
};

struct DropResolution {
    DropOutcome outcome = DropOutcome::Cancel;
    int col = -1; ///< Landing cell for Move, CreatePage and Swap
    int row = -1;
    /// A CreatePage lands before the first page, in the leftmost columns its
    /// span allows, instead of past the last one.
    bool prepend_page = false;
    /// For a Swap, the widget the entry lands on, at the cell it moves to.
    GridPlacement swapped{};
};

/**
 * @brief Resolve a released drag.
 *
 * - It creates a page when cross_page_drop_creates_page() holds for the scope
 *   the release lands in. On the next-page slot the entry lands at the previewed
 *   cell; from the last page's border, in the rightmost columns its span allows,
 *   on the previewed row; from the first page's left border, in the leftmost
 *   columns on the previewed row, on a page that lands before the first.
 *   A landing cell that does not fit an empty page creates nothing.
 * - Otherwise, on a config page, it moves the entry to the previewed cell when
 *   that cell or the page differs from the origin and the span fits
 *   @p occupancy there.
 * - On the origin page, a previewed cell that overlaps exactly one widget
 *   swaps the two: the entry lands on that widget's cell and the widget on the
 *   entry's origin cell, when both fit the page with each other in place.
 * - Anything else commits nothing: off the origin page the drag returns to it,
 *   on the origin page it snaps back.
 *
 * @param in The release.
 * @param occupancy The scoped page's widgets, the dragged one left out, on the
 *        grid the page is laid out on. Its dimensions also bound a created
 *        page's landing cell.
 */
DropResolution resolve_drop(const DropInput& in, const GridLayout& occupancy);

struct PanelWidgetDef;

/**
 * @brief Why @p def cannot sit at a track rectangle, or empty when it can.
 *
 * The span must lie within the def's declared range, cover whole cells on an
 * axis without half-cell support, and fit free tracks on @p occupancy, which
 * holds the page's other widgets.
 */
std::string placement_refusal(const PanelWidgetDef& def, const GridLayout& occupancy, int col,
                              int row, int colspan, int rowspan);

} // namespace helix
