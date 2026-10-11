// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>

namespace helix::ui {

/// Portrait preview card height for the print file detail view.
///
/// The preview is half its width (2:1). When the options below it overflow,
/// it shrinks until the scroll area's visible bottom edge shows the first
/// tile row fully and cuts the second row through its middle (or all of the
/// content when it ends sooner). When the rows already fit under the base
/// height, the edge is only nudged out of grid gaps and tile outer quarters
/// into a middle half, by at most gap + tile_h / 2. Never below width/3,
/// where the preview stops reading as a model.
inline int decide_detail_portrait_preview(int width, int avail_h, int content_h, int grid_top,
                                          int tile_h, int gap) {
    const int base = width / 2;
    const int min_h = width / 3;
    if (content_h <= avail_h - base) {
        return base;
    }
    if (tile_h <= 0) {
        return base;
    }
    const int pitch = tile_h + gap;
    // The edge target: the middle of row 2, or the end of the content.
    const int target_edge = std::min(grid_top + pitch + tile_h / 2, content_h);
    const int h = avail_h - target_edge;
    if (h < base) {
        return std::max(h, min_h);
    }
    // The rows fit under the base height, so the edge only needs the nudge.
    const int edge = avail_h - base;
    const int in_row = (edge - grid_top) % pitch;
    const int lo = tile_h / 4;
    const int hi = tile_h - tile_h / 4;
    if (in_row >= lo && in_row <= hi) {
        return base;
    }
    // Moving the edge down past `hi` reaches the next tile's middle half at
    // pitch + lo; below `lo` it starts at lo.
    const int shift = in_row < lo ? lo - in_row : pitch + lo - in_row;
    return std::max(base - shift, min_h);
}

} // namespace helix::ui
