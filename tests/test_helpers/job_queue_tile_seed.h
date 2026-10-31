// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../test_helpers/job_queue_state_test_access.h"
#include "../test_helpers/job_queue_subjects_fixture.h"
#include "app_globals.h"
#include "job_queue_state.h"
#include "panel_widget_manager.h"
#include "printer_state.h"

#include <functional>
#include <string>
#include <utility>

namespace helix {

/// A JobQueueState installed as the app's, with the printer reporting the queue
/// component available. What the job queue tile reads its state from. Destruction runs
/// in reverse: the state pointer clears first, then the subject names leave helix-xml's
/// scope, then the state dies.
struct JobQueueTileSeed {
    /// @p settle drains the update queue and pumps LVGL; the owning fixture supplies it.
    explicit JobQueueTileSeed(std::function<void()> settle) : settle_(std::move(settle)) {
        PanelWidgetManager::instance().init_widget_subjects();
        jqs.init_subjects();
        set_component_available(true);
        set_print_active(false);
    }
    ~JobQueueTileSeed() {
        set_print_active(false);
        set_component_available(false);
    }

    JobQueueTileSeed(const JobQueueTileSeed&) = delete;
    JobQueueTileSeed& operator=(const JobQueueTileSeed&) = delete;

    JobQueueState jqs{nullptr, nullptr};
    ScopedJobQueueSubjects subject_guard;
    ScopedJobQueueState scoped_state{&jqs};

    static void set_component_available(bool on) {
        if (lv_subject_t* subject = lv_xml_get_subject(nullptr, "printer_has_job_queue")) {
            lv_subject_set_int(subject, on ? 1 : 0);
        }
    }
    void set_print_active(bool on) {
        lv_subject_set_int(get_printer_state().print_state().get_print_active_subject(),
                           on ? 1 : 0);
        settle_();
    }
    /// A queue response with @p n jobs (file-0.gcode ...) in @p queue_state.
    void deliver(int n, const char* queue_state = "ready") {
        JobQueueStatus status = status_with(n);
        status.queue_state = queue_state;
        JobQueueStateTestAccess::deliver_status(jqs, status);
        settle_();
    }
    /// A response whose first job is named @p first_name.
    void deliver_named(int n, const std::string& first_name) {
        std::vector<std::string> files = numbered_files(n);
        if (!files.empty()) {
            files.front() = first_name;
        }
        JobQueueStateTestAccess::deliver_status(jqs, status_of(files));
        settle_();
    }
    void set_automatic(bool on) {
        JobQueueStateTestAccess::set_automatic_transition(jqs, on);
        settle_();
    }

  private:
    std::function<void()> settle_;
};

} // namespace helix
