// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file print_progress_text.h
 * @brief The print-status panel's progress text.
 *
 * Owns the string subjects the progress card binds to (layer, filament used,
 * elapsed, remaining, ETA, speed, flow) and the formatting behind them. The
 * panel decides when a value is worth showing; this decides how it reads.
 *
 * @threading Main thread only.
 */

#pragma once

#include "lvgl.h"
#include "print_lifecycle_state.h"
#include "printer_state.h"
#include "subject_managed_panel.h"

#include <string>

namespace helix::ui {

class PrintProgressText {
  public:
    PrintProgressText(PrinterState& printer_state, const PrintLifecycleState& lifecycle)
        : printer_state_(printer_state), lifecycle_(lifecycle) {}

    PrintProgressText(const PrintProgressText&) = delete;
    PrintProgressText& operator=(const PrintProgressText&) = delete;

    /// Initialise and register every subject. @p subjects frees them.
    void init_subjects(SubjectManager& subjects);

    /// "Layer n / m" from the lifecycle's layer counters.
    void refresh_layer();
    /// Blank the layer text (preparing has no layers yet).
    void clear_layer();
    /// Filament used so far, empty when none.
    void refresh_filament_used();
    /// Speed and flow, in percent or physical units per the display setting.
    void refresh_speed_flow();

    void show_elapsed(int seconds);
    void show_remaining(int seconds);
    /// Remaining time plus the wall-clock time it ends at.
    void show_time_left(int seconds);

  private:
    PrinterState& printer_state_;
    const PrintLifecycleState& lifecycle_;

    lv_subject_t layer_text_subject_{};
    lv_subject_t filament_used_text_subject_{};
    lv_subject_t elapsed_subject_{};
    lv_subject_t remaining_subject_{};
    lv_subject_t eta_subject_{};
    lv_subject_t speed_subject_{};
    lv_subject_t flow_subject_{};

    char layer_text_buf_[80] = "Layer 0 / 0";
    char filament_used_text_buf_[32] = "";
    char elapsed_buf_[32] = "0h 00m";
    char remaining_buf_[32] = "0h 00m";
    char eta_buf_[32] = "";
    char speed_buf_[32] = "100%";
    char flow_buf_[32] = "100%";
};

} // namespace helix::ui
