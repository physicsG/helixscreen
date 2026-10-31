// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_modal.h"

#include <cstddef>

namespace helix::ui {

/// Confirms a per-tool stepper resonance calibration and lets the user pick
/// the tool it runs with (helix::stepper_resonance_cal). A whole-machine
/// routine, or a printer with one tool, confirms through modal_confirm instead.
class StepperResonanceCalModal : public Modal {
  public:
    explicit StepperResonanceCalModal(size_t tool_count) : tool_count_(tool_count) {}

    const char* get_name() const override {
        return "Stepper Resonance Cal";
    }
    const char* component_name() const override {
        return "stepper_resonance_cal_modal";
    }

    /// One-shot owned show over the active screen. False if the XML failed.
    static bool show_owned(size_t tool_count, const char* message);

  protected:
    void on_show() override;
    void on_ok() override;

  private:
    /// The dropdown's rows are tools 0..tool_count_-1.
    size_t tool_count_;
};

/// Registers the Calibration row's "on_stepper_resonance_cal_clicked" callback.
void init_stepper_resonance_cal_row_handler();

/// Send the calibration with @p tool on the carriage, homing first when the
/// routine needs it. Refused while a print holds the machine.
void start_stepper_resonance_cal(int tool);

} // namespace helix::ui
