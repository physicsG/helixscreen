// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Pure layout decision for the job queue home tile.
//
// Free of LVGL so it is unit-testable without a display. JobQueueWidget measures the
// minimum box each rung draws in and feeds it here; this header picks the rung and the
// flags the XML binds. The tile offers Strip, Cell, Wide and Full.

#include "job_queue_tile_state.h"
#include "src/ui/panel_widgets/size_rung.h"

#include <algorithm>

namespace helix {

/// Jobs after the first that the Full rung lists, at most.
inline constexpr int kJobQueueTileListRows = 3;

struct JobQueueTileLayoutInput {
    int content_w = 0; ///< the box the content draws in, chrome already subtracted
    int content_h = 0;
    JobQueueTileState state = JobQueueTileState::NotLoaded;
    int job_count = 0;
    RungNeeds needs;     ///< offers strip, cell, wide and full only
    int more_line_h = 0; ///< what "+N more" adds under the name at Cell, gap included
    int list_row_h = 0;  ///< what one listed job adds at Full, gap included
    int auto_line_h = 0; ///< what "Starts automatically" adds at Cell, where it wraps
    /// What the next job's name adds at Cell beyond its first line, when it wraps.
    int cell_name_extra_h = 0;
};

/// The published verdict. `rung` is SizeRung's int; the flags are 0/1 subjects.
struct JobQueueTileVerdict {
    SizeRung rung = SizeRung::Strip;
    bool fits = false;
    bool show_thumbnail = false; ///< the next job's picture, while it waits or loads
    bool show_detail = false;    ///< the estimate while waiting, "Next: name" while printing
    bool show_auto_line = false; ///< "Starts automatically"
    bool show_more = false;      ///< "+N more"
    int more_count = 0;          ///< queued jobs neither named nor listed
    int list_rows = 0;           ///< following jobs listed under the next one
    bool button_icon_only = false;
};

[[nodiscard]] constexpr JobQueueTileVerdict
decide_job_queue_tile_layout(const JobQueueTileLayoutInput& in) {
    JobQueueTileVerdict v;
    v.rung = choose_size_rung(in.content_w, in.content_h, in.needs);
    v.fits = holds_any_rung(in.content_w, in.content_h, in.needs);
    const int r = static_cast<int>(v.rung);
    const bool has_room = r >= static_cast<int>(SizeRung::Cell);
    const bool waiting = in.state == JobQueueTileState::WaitingForStart;
    const bool printing =
        in.state == JobQueueTileState::AutoNext || in.state == JobQueueTileState::Printing;
    // A Cell is too narrow to put a picture beside a legible name.
    v.show_thumbnail = r >= static_cast<int>(SizeRung::Wide) &&
                       (waiting || in.state == JobQueueTileState::Starting);
    // A waiting job's detail is its estimate, which earns the thumbnail's width.
    v.show_detail = (waiting && r >= static_cast<int>(SizeRung::Wide)) || (printing && has_room);
    // At Cell the line wraps under the hero and shows only where the height holds it.
    const int cell_h = in.needs.cell.h + in.cell_name_extra_h;
    v.show_auto_line = in.state == JobQueueTileState::AutoNext && has_room &&
                       (v.rung != SizeRung::Cell || in.content_h >= cell_h + in.auto_line_h);
    const bool has_queue = in.state != JobQueueTileState::Empty &&
                           in.state != JobQueueTileState::NotLoaded &&
                           in.state != JobQueueTileState::Unavailable;
    if (v.rung == SizeRung::Full && has_queue) {
        // As many whole rows as the height below the Wide content holds.
        const int room = in.content_h - in.needs.wide.h;
        const int fit = in.list_row_h > 0 ? room / in.list_row_h : kJobQueueTileListRows;
        v.list_rows =
            std::max(0, std::min({kJobQueueTileListRows, in.job_count - 1, std::max(0, fit)}));
    }
    // "+N more" counts only what the tile does not already show.
    const bool more_room = v.rung != SizeRung::Cell || in.content_h >= cell_h + in.more_line_h;
    if (waiting && has_room && more_room) {
        v.more_count = std::max(0, in.job_count - 1 - v.list_rows);
    }
    v.show_more = v.more_count > 0;
    v.button_icon_only = v.rung == SizeRung::Strip;
    return v;
}

} // namespace helix
