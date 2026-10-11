// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "helix-xml/src/xml/lv_xml.h"
#include "job_queue_state.h"
#include "static_subject_registry.h"

#include <string>

namespace helix {

/// Drops every JobQueueState subject name out of helix-xml's global scope so
/// no later test resolves a pointer into this fixture's stack frame. Declare
/// AFTER the JobQueueState and BEFORE any widget/harness that observes the
/// subjects, so destruction runs in the reverse order: observers detach
/// first, then the names go away.
struct ScopedJobQueueSubjects {
    ~ScopedJobQueueSubjects() {
        StaticSubjectRegistry::instance().deinit_one("JobQueueState");
        lv_xml_unregister_subject(nullptr, "job_queue_count");
        lv_xml_unregister_subject(nullptr, "job_queue_summary_text");
        lv_xml_unregister_subject(nullptr, "job_queue_state_text");
        lv_xml_unregister_subject(nullptr, "job_queue_up_next_text");
        lv_xml_unregister_subject(nullptr, "job_queue_start_next_text");
    }
};

/// A "ready" queue with n jobs: ids job-0.., filenames file-N.gcode.
inline JobQueueStatus status_with(int n) {
    JobQueueStatus s;
    s.queue_state = "ready";
    for (int i = 0; i < n; ++i) {
        s.queued_jobs.push_back({"job-" + std::to_string(i), "file-" + std::to_string(i) + ".gcode",
                                 1000.0 + i, 30.0 * (i + 1)});
    }
    return s;
}

} // namespace helix
