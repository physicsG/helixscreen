// tests/test_helpers/grid_edit_mode_test_access.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "grid_edit_mode.h"

namespace helix {

/// Reads the snap target GridEditMode computed for the current drag.
///
/// snap_preview_col_/row_ are the direct output of handle_drag_move()'s cell
/// computation and the values handle_drag_end() commits to config, so they are
/// what a drag test needs to see. Both reset to -1 on drag start and on exit.
struct GridEditModeTestAccess {
    static int snap_col(const GridEditMode& em) {
        return em.snap_preview_col_;
    }
    static int snap_row(const GridEditMode& em) {
        return em.snap_preview_row_;
    }

    /// The pixel-tracking resize overlay a live resize drag creates. Reads as
    /// nullptr once commit_resize_with_snap() has handed it to the snap
    /// animation, so a lifetime test has to latch it before committing.
    static lv_obj_t* resize_preview(const GridEditMode& em) {
        return em.resize_preview_;
    }

    /// Create resize_preview_ the way handle_resize_move() does. A full drag
    /// would reach the same call through the indev, but the snap animation's
    /// lifetime does not depend on how the preview came to exist.
    static void make_resize_preview(GridEditMode& em, int x, int y, int w, int h) {
        em.update_resize_preview_px(x, y, w, h, true);
    }

    /// Run the resize-commit path — the one that starts the snap animation.
    static void commit_resize(GridEditMode& em, const GridEditMode::ResizeResult& result) {
        em.commit_resize_with_snap(result);
    }
};

} // namespace helix
