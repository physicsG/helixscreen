// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"
#include "settings_manager.h"

namespace helix::test {

/// show_widget_labels on for a scope, so a tile's caption is part of what it decides.
class ScopedWidgetLabelsShown {
  public:
    ScopedWidgetLabelsShown() {
        SettingsManager::instance().init_subjects();
        subject_ = SettingsManager::instance().subject_show_widget_labels();
        original_ = lv_subject_get_int(subject_);
        lv_subject_set_int(subject_, 1);
    }
    ~ScopedWidgetLabelsShown() {
        lv_subject_set_int(subject_, original_);
    }
    ScopedWidgetLabelsShown(const ScopedWidgetLabelsShown&) = delete;
    ScopedWidgetLabelsShown& operator=(const ScopedWidgetLabelsShown&) = delete;

  private:
    lv_subject_t* subject_ = nullptr;
    int32_t original_ = 0;
};

} // namespace helix::test
