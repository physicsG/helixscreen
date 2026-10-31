// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "panel_widget.h"
#include "src/ui/panel_widgets/display_message_layout.h"

namespace helix {

/// Home widget for Klipper's display_status.message (M117 / SET_DISPLAY_TEXT), shown
/// whether or not a print is running. The text binds the display_message subject
/// PrinterPrintState already publishes; this class only decides, per tile size,
/// whether the message wraps or scrolls on one line and whether the icon fits, and
/// publishes that for panel_widget_display_message.xml. It has no tap action: the
/// print status rows that show the same message have none either.
class DisplayMessageWidget : public PanelWidget {
  public:
    DisplayMessageWidget() = default;
    ~DisplayMessageWidget() override;

    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    void on_size_changed(int colspan, int rowspan, int width_px, int height_px) override;
    bool fits_at(int width_px, int height_px) const override;
    const char* id() const override {
        return "display_message";
    }

  private:
    DisplayMessageVerdict decide(int width_px, int height_px) const;
    void publish();

    DisplayMessageVerdict verdict_;
};

void register_display_message_widget();

} // namespace helix
