// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_pause_markers.h
 * @brief Scheduled-pause ticks for the print progress surfaces.
 *
 * Both progress surfaces that fill from `print_progress_display` can carry a
 * tick per scheduled pause found in the print's gcode (M600 / PAUSE / M601 /
 * M0; see gcode_pause_scan.h): the linear `lv_bar` and the 270-degree
 * `lv_arc`. A tick is placed at the pause's fraction on the SAME axis the bar
 * is filling on — PrinterPrintState derives that axis from the file, and
 * markers render only while the list belongs to the file being printed
 * (PrinterPrintState::pause_markers_match_current_file()). No list means no
 * ticks: an external start whose gcode was never fetched degrades to absent,
 * never to wrong.
 *
 * Draw hooks have no declarative equivalent (.claude/rules/declarative-ui.md),
 * so this is a sanctioned C++ site; the ticks read live state at draw time
 * rather than caching fractions, and re-render whenever the pause list's
 * version subject bumps.
 *
 * @threading Main thread only — attaches LVGL event callbacks and observers.
 */

#pragma once

#include "lvgl/lvgl.h"

namespace helix {
class PrinterState;
}

namespace helix::ui {

/// Attach pause ticks to a linear progress bar (lv_bar) bound to
/// print_progress_display. Safe to call with a null @p bar (no-op).
/// The bar keeps filling from its XML bind_value; this only ADDS ticks.
void attach_bar_pause_markers(lv_obj_t* bar, helix::PrinterState& printer_state);

/// Attach pause ticks to a progress arc (lv_arc — e.g. helix_progress_arc)
/// bound to print_progress_display. Ticks are radial lines crossing the track
/// at `start_angle + fraction * sweep`. Safe to call with a null @p arc.
void attach_arc_pause_markers(lv_obj_t* arc, helix::PrinterState& printer_state);

} // namespace helix::ui
