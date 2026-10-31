// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string_view>

namespace helix {

/// Moonraker's `job_queue.queue_state`, as the int published on the
/// `job_queue_run_state` subject. Anything unrecognised reads as Paused.
enum class QueueRunState : int { Ready = 0, Loading = 1, Starting = 2, Paused = 3 };

[[nodiscard]] constexpr QueueRunState parse_queue_run_state(std::string_view state) {
    if (state == "ready")
        return QueueRunState::Ready;
    if (state == "loading")
        return QueueRunState::Loading;
    if (state == "starting")
        return QueueRunState::Starting;
    return QueueRunState::Paused;
}

/// What the home queue tile shows. One value drives its icon, text and button.
enum class JobQueueTileState : int {
    Unavailable = 0,
    NotLoaded = 1,
    Empty = 2,
    WaitingForStart = 3,
    AutoNext = 4,
    Printing = 5,
    Starting = 6,
};

enum class JobQueueTileAction : int { None = 0, StartNext = 1 };

struct JobQueueTileInputs {
    bool component_available = false;
    bool loaded = false; ///< a queue response has arrived at least once
    int job_count = 0;
    QueueRunState queue_state = QueueRunState::Paused;
    bool printer_busy = false; ///< print_active: printing or paused
    bool automatic_transition = false;
    bool start_in_flight = false; ///< the tile's own start is checking or starting a job
};

/// First match wins. The tile's own start reads as Starting from the tap, so
/// the button is gone while the job is checked. An idle printer with queued
/// jobs is always WaitingForStart: Moonraker leaves the queue "paused" after
/// every job it loads unless automatic transition is on, so a finished job
/// and a user pause cannot be told apart.
/// AutoNext needs a running print, since Moonraker only advances on completion.
[[nodiscard]] constexpr JobQueueTileState job_queue_tile_state(const JobQueueTileInputs& in) {
    if (!in.component_available)
        return JobQueueTileState::Unavailable;
    if (!in.loaded)
        return JobQueueTileState::NotLoaded;
    if (in.job_count <= 0)
        return JobQueueTileState::Empty;
    if (in.start_in_flight || in.queue_state == QueueRunState::Loading ||
        in.queue_state == QueueRunState::Starting)
        return JobQueueTileState::Starting;
    if (in.printer_busy && in.automatic_transition && in.queue_state == QueueRunState::Ready)
        return JobQueueTileState::AutoNext;
    if (in.printer_busy)
        return JobQueueTileState::Printing;
    return JobQueueTileState::WaitingForStart;
}

[[nodiscard]] constexpr JobQueueTileAction job_queue_tile_action(JobQueueTileState state) {
    return state == JobQueueTileState::WaitingForStart ? JobQueueTileAction::StartNext
                                                       : JobQueueTileAction::None;
}

} // namespace helix
