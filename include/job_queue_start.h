// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace helix {

struct JobQueueEntry;

/// What a tap on an "Up next" line should do, given whether the printer can
/// take a job right now.
enum class UpNextTapAction {
    OpenQueueModal, ///< Printer busy or blocked: show the queue instead
    StartNextJob,   ///< Printer free: hand it the first queued job
};

/// Start the first queued job through print-select's file view. The completion
/// modal's "Start next" button and the "Up next" line's idle tap both go
/// through here. The home tile starts through QueuedJobStarter instead, which
/// shares this path's option rule, gates, start call and bookkeeping.
void start_next_queued_job();

/// Show the process-wide job queue modal.
void open_job_queue_modal();

/// Open @p job in print-select's file view, seeded with its saved options, to
/// start with a Print tap: the two-tap path start_next_queued_job() takes for
/// the first job.
void open_queued_job_in_file_view(const JobQueueEntry& job);

/// Route an "Up next" tap through decide_up_next_tap(): start the job when
/// the printer can take one, otherwise open the job queue modal.
void handle_up_next_tap();

/// Pure decision half of handle_up_next_tap().
UpNextTapAction decide_up_next_tap(bool can_start_new_print);

/// Register the XML event callbacks this module owns ("on_up_next_tap",
/// "on_print_complete_start_next"). Idempotent; must run before any XML that
/// references them is created — subject init does that.
void register_job_queue_start_callbacks();

} // namespace helix
