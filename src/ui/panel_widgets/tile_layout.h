// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file tile_layout.h
 * @brief Pure sizing decision for a centred-icon home tile.
 *
 * Measured pixels in, a verdict out. No LVGL, no widget, no subjects, so every
 * boundary below is unit-testable without a display. The widget measures in the
 * faces it actually renders and publishes the verdict as subjects; XML binds
 * appearance to those. See docs/devel/PANEL_WIDGET_GUIDE.md.
 */

#include <algorithm>

namespace helix {

/// Rungs on the icon ladder: xs, sm, md, lg, xl, and xxl above the tier's
/// authored faces. The same index selects the value and label faces the tile
/// pairs with that rung, so one number drives every face on the tile and the
/// ladders cannot drift apart.
inline constexpr int kTileRungs = 6;

enum class TileDirection : int {
    Column = 0, ///< icon above the value
    Row = 1,    ///< icon beside the value
};

enum class TileLabelRung : int {
    None = 0,
    Label = 1,
};

/// Measured content at one rung, in the faces the tile renders at that rung.
///
/// The two value widths are both WORST CASES, never a live reading: a size
/// accepted while the tile reads 95 must still draw 888. `value_full_w` is the
/// widest "current / target" the tile can ever show, `value_current_w` the
/// widest current half alone, which is what it draws once the target is
/// dropped.
struct TileRungMetrics {
    int icon_w = 0;
    int icon_h = 0;
    int value_full_w = 0;
    int value_current_w = 0;
    int value_h = 0;
    int label_w = 0;
    int label_h = 0;
};

struct TileVerdict {
    int icon_rung = 2;
    TileLabelRung label = TileLabelRung::Label;
    TileDirection direction = TileDirection::Column;
    bool show_target = true;
    /// Whether the tile can draw its icon and its widest value at this size.
    /// Edit mode refuses a size where this is false.
    bool fits = true;
};

namespace detail {

/// One candidate presentation: which optional parts are still drawn.
struct TileCandidate {
    bool label;
    bool target;
};

/// Does rung @p m draw this candidate inside @p avail_w x @p avail_h?
inline bool tile_candidate_fits(const TileRungMetrics& m, TileDirection dir, TileCandidate cand,
                                bool has_value, int avail_w, int avail_h, int gap_px) {
    const int value_w = !has_value ? 0 : (cand.target ? m.value_full_w : m.value_current_w);
    const int value_h = has_value ? m.value_h : 0;
    const int label_w = cand.label ? m.label_w : 0;
    const int label_h = cand.label ? m.label_h : 0;

    int need_w = 0;
    int need_h = 0;
    if (dir == TileDirection::Column) {
        need_w = std::max({m.icon_w, value_w, label_w});
        need_h =
            m.icon_h + (has_value ? gap_px + value_h : 0) + (cand.label ? gap_px + label_h : 0);
    } else {
        const int side_w = std::max(value_w, label_w);
        need_w = m.icon_w + (side_w > 0 ? gap_px + side_w : 0);
        const int side_h = value_h + (has_value && cand.label ? gap_px : 0) + label_h;
        need_h = std::max(m.icon_h, side_h);
    }
    return need_w <= avail_w && need_h <= avail_h;
}

} // namespace detail

/**
 * @brief Pick the rung, direction and content a tile can draw at this size
 *
 * Down to @p authored_rung, content is surrendered in a fixed order and only
 * when nothing at those rungs can keep it: the target half first, then the
 * label. Within each step the largest rung that fits wins, so a roomy tile
 * grows its glyph and keeps its text. Below the authored rung the glyph is what
 * gives way last: a tile drops its text before it shrinks the icon every other
 * tile draws at, and only a box that cannot hold that icon at all shrinks it.
 * Column is preferred and Row is the fallback for a box too short to stack, so
 * direction follows fit rather than aspect ratio.
 *
 * A label that is the tile's identity (which fan, which sensor: the only thing
 * telling two such tiles apart) is surrendered last instead: the glyph shrinks
 * all the way down with the label kept, and only a box that cannot hold the
 * label at the smallest rung drops it.
 *
 * `fits` is false only when even the smallest rung, with everything optional
 * surrendered, cannot draw the icon and the widest value. It is MONOTONIC in
 * both axes by construction: it is a disjunction over a fixed candidate set,
 * each member of which compares a constant need against the available box.
 *
 * @param avail_w        usable width in px (the grid's arithmetic extent)
 * @param avail_h        usable height in px
 * @param gap_px         gap between stacked parts
 * @param rungs          measured metrics, one per rung, smallest first
 * @param has_value      false for a tile that shows only an icon and a label
 * @param labels_enabled the user setting; size may hide a label, never show one
 * @param authored_rung  the rung the tier draws a tile's icon at by default
 * @param row_inset      width a row gives to its side insets, which a column does not
 * @param label_is_identity the label says which tile this is, so it outlasts the glyph
 */
inline TileVerdict decide_tile_layout(int avail_w, int avail_h, int gap_px,
                                      const TileRungMetrics rungs[kTileRungs], bool has_value,
                                      bool labels_enabled, int authored_rung = 0, int row_inset = 0,
                                      bool label_is_identity = false) {
    using detail::TileCandidate;

    // Ordered by what the tile gives up, most complete first. A tile that has
    // already surrendered its label has surrendered the target too, so the
    // label-off/target-on pairing is deliberately not offered.
    const TileCandidate with_labels[] = {{true, true}, {true, false}, {false, false}};
    const TileCandidate without_labels[] = {{false, true}, {false, false}};
    const TileCandidate* candidates = labels_enabled ? with_labels : without_labels;
    const int candidate_count = labels_enabled ? 3 : 2;

    const TileDirection directions[] = {TileDirection::Column, TileDirection::Row};
    const int floor_rung = std::clamp(authored_rung, 0, kTileRungs - 1);
    auto width_for = [&](TileDirection dir) {
        return dir == TileDirection::Row ? avail_w - row_inset : avail_w;
    };

    auto verdict_for = [&](int r, int c, TileDirection dir) {
        TileVerdict v;
        v.icon_rung = r;
        v.label = candidates[c].label ? TileLabelRung::Label : TileLabelRung::None;
        v.direction = dir;
        v.show_target = has_value && candidates[c].target;
        v.fits = true;
        return v;
    };

    // Candidates [first, last) are tried over every rung before any later one:
    // an identity label splits them into those that keep it (a prefix, by the
    // order above) and those that do not, everything else is one group.
    int kept = candidate_count;
    if (label_is_identity) {
        kept = 0;
        while (kept < candidate_count && candidates[kept].label) {
            ++kept;
        }
    }
    const int groups[][2] = {{0, kept}, {kept, candidate_count}};
    for (const auto& [first, last] : groups) {
        // At or above the authored rung: keep content, grow the glyph into room.
        for (int c = first; c < last; ++c) {
            for (TileDirection dir : directions) {
                for (int r = kTileRungs - 1; r >= floor_rung; --r) {
                    if (detail::tile_candidate_fits(rungs[r], dir, candidates[c], has_value,
                                                    width_for(dir), avail_h, gap_px)) {
                        return verdict_for(r, c, dir);
                    }
                }
            }
        }
        // Below it: the glyph shrinks one rung at a time, keeping what content fits.
        for (int r = floor_rung - 1; r >= 0; --r) {
            for (int c = first; c < last; ++c) {
                for (TileDirection dir : directions) {
                    if (detail::tile_candidate_fits(rungs[r], dir, candidates[c], has_value,
                                                    width_for(dir), avail_h, gap_px)) {
                        return verdict_for(r, c, dir);
                    }
                }
            }
        }
    }

    // Nothing draws here. Report the most forgiving arrangement so a caller
    // that renders anyway clips as little as possible: the smallest glyph,
    // stacked along whichever axis the box has more of once a row's inset is
    // paid.
    TileVerdict v;
    v.icon_rung = 0;
    v.label = TileLabelRung::None;
    v.direction =
        avail_h >= width_for(TileDirection::Row) ? TileDirection::Column : TileDirection::Row;
    v.show_target = false;
    v.fits = false;
    return v;
}

} // namespace helix
