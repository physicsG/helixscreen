// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace helix::ui {

/// How a wrapping grid of equal tiles should be arranged inside its container.
struct TileGridFit {
    int cols = 1;
    int rows = 1;
    /// Whole-pixel tile size: `cols` tiles and their gaps never exceed the
    /// box width, nor `rows` tiles and their gaps its height.
    int tile_w = 0;
    int tile_h = 0;
    /// Every tile meets the minimum size, so it has room for its label.
    bool fits = false;
};

/**
 * @brief Columns and rows for `count` equal tiles in a `content_w` x `content_h` box.
 *
 * Every arrangement up to `max_cols` x `max_rows` that holds all the tiles is
 * scored by how far its tile clears the minimum on its tighter axis, i.e.
 * min(tile_w / min_tile_w, tile_h / min_tile_h). The minimum's own aspect
 * stands for what an icon over a label needs, so the winner is the biggest
 * tile in that shape: a wide box gets more columns, a tall one more rows.
 * Ties go to the arrangement with fewer empty cells, then to more columns,
 * since a wider tile keeps a label on one line.
 *
 * `fits` is false when even the best arrangement leaves its tile below the
 * minimum; callers drop the labels then rather than clip them.
 *
 * @param gap_x Space between columns in px.
 * @param gap_y Space between rows in px.
 */
inline TileGridFit fit_tile_grid(int count, int content_w, int content_h, int gap_x, int gap_y,
                                 int min_tile_w, int min_tile_h, int max_cols = 4,
                                 int max_rows = 4) {
    TileGridFit best;
    if (count <= 0 || content_w <= 0 || content_h <= 0 || min_tile_w <= 0 || min_tile_h <= 0) {
        return best;
    }
    double best_score = -1.0;
    int best_empty = 0;
    bool found = false;
    for (int cols = 1; cols <= max_cols && cols <= count; ++cols) {
        const int rows = (count + cols - 1) / cols;
        if (rows > max_rows) {
            continue;
        }
        const double tile_w = static_cast<double>(content_w - (cols - 1) * gap_x) / cols;
        const double tile_h = static_cast<double>(content_h - (rows - 1) * gap_y) / rows;
        const double score_w = tile_w / min_tile_w;
        const double score_h = tile_h / min_tile_h;
        const double score = score_w < score_h ? score_w : score_h;
        const int empty = cols * rows - count;
        constexpr double kEps = 1e-9;
        // Columns ascend, so on a full tie the later (wider) candidate wins.
        const bool better = !found || score > best_score + kEps ||
                            (score > best_score - kEps && empty <= best_empty);
        if (better) {
            found = true;
            best_score = score;
            best_empty = empty;
            best.cols = cols;
            best.rows = rows;
            best.fits = score >= 1.0;
        }
    }
    if (!found) {
        // Nothing within max_cols x max_rows holds every tile: use the widest grid.
        best.cols = max_cols;
        best.rows = (count + max_cols - 1) / max_cols;
    }
    // Integer division floors, so the tiles plus their gaps fit the box exactly.
    const int free_w = content_w - (best.cols - 1) * gap_x;
    const int free_h = content_h - (best.rows - 1) * gap_y;
    best.tile_w = free_w > 0 ? free_w / best.cols : 0;
    best.tile_h = free_h > 0 ? free_h / best.rows : 0;
    return best;
}

/// One key size for a fixed grid of keys.
struct KeyGridFit {
    int key_w = 0;
    int key_h = 0;
};

/**
 * @brief Key size for a fixed `cols` x `rows` grid of keys in a box.
 *
 * Unlike fit_tile_grid() the arrangement is given, and the keys do not fill
 * their cells: a key is square at the tighter axis's cell size and stretches
 * the other way to at most `max_aspect` times that, so a wide or tall box
 * leaves room around the grid instead of drawing slabs. Either side is lifted
 * to `min_key` where its cell has that much room, and never exceeds its cell.
 *
 * @param cols, rows Grid size in keys; fractional for a key half as wide.
 * @param free_w, free_h The box less the gaps between keys, in px.
 */
inline KeyGridFit fit_key_grid(double cols, double rows, int free_w, int free_h, int min_key,
                               double max_aspect = 1.4) {
    KeyGridFit fit;
    if (cols <= 0 || rows <= 0 || free_w <= 0 || free_h <= 0) {
        return fit;
    }
    const double cell_w = free_w / cols;
    const double cell_h = free_h / rows;
    const double square = cell_w < cell_h ? cell_w : cell_h;
    auto side = [&](double cell) {
        double v = square * max_aspect;
        if (v > cell)
            v = cell;
        if (v < min_key)
            v = cell < min_key ? cell : min_key;
        return static_cast<int>(v);
    };
    fit.key_w = side(cell_w);
    fit.key_h = side(cell_h);
    return fit;
}

} // namespace helix::ui
