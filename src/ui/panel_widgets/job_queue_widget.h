// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_job_queue_modal.h"
#include "ui_observer_guard.h"

#include "async_lifetime_guard.h"
#include "panel_widget.h"

#include <memory>

namespace helix {

class JobQueueWidget : public PanelWidget {
  public:
    JobQueueWidget();
    ~JobQueueWidget() override;

    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    void on_activate() override;
    void on_deactivate() override;
    void on_size_changed(int colspan, int rowspan, int width_px, int height_px) override;
    const char* id() const override {
        return "job_queue";
    }

    /// Open the job queue modal (called from XML event callback)
    void open_modal();

  private:
    lv_obj_t* widget_obj_ = nullptr;
    lv_obj_t* parent_screen_ = nullptr;
    lv_obj_t* job_list_container_ = nullptr;

    ObserverGuard count_observer_;
    int current_size_mode_ = 1;         // 0=compact, 1=normal, 2=expanded
    bool list_rebuild_pending_ = false; ///< Coalesces rapid count observer notifications

    /// Guards next-tick callbacks from accessing a detached widget
    helix::AsyncLifetimeGuard lifetime_;

    void rebuild_job_list();

    helix::JobQueueModal job_queue_modal_;
};

} // namespace helix
