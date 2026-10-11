// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_widget_ref.h"

#include "panel_widget.h"

namespace helix {

class ClockWidget : public PanelWidget {
  public:
    ClockWidget();
    ~ClockWidget() override;

    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    void on_size_changed(int colspan, int rowspan, int width_px, int height_px) override;
    void on_activate() override;
    void on_deactivate() override;
    const char* id() const override {
        return "clock";
    }

  private:
    helix::ui::WidgetRef widget_obj_;
    helix::ui::WidgetRef parent_screen_;
    lv_timer_t* clock_timer_ = nullptr;

    // Update all clock subjects with current time/date/uptime
    void update_clock();

    // Static timer callback
    static void clock_timer_cb(lv_timer_t* timer);
};

} // namespace helix
