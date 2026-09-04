// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_wizard_touch_calibration.h"

#include "touch_calibration_panel.h"

/**
 * @brief Test-only access to WizardTouchCalibrationStep internals.
 *
 * The wizard owns a private TouchCalibrationPanel. Driving that panel directly
 * (via helix::TouchCalibrationPanelTestAccess) is the only way to exercise the
 * wizard's verify-entry auto-accept wiring (#1029) without standing up the full
 * LVGL/XML screen — the panel reaching COMPLETE proves the wizard wired
 * set_verify_entry_callback to accept() on the real commit path.
 *
 * Lives in the global namespace to match WizardTouchCalibrationStep (which is
 * not namespaced), so the `friend class WizardTouchCalibrationTestAccess;`
 * declaration in the wizard header resolves to this class.
 */
class WizardTouchCalibrationTestAccess {
  public:
    static helix::TouchCalibrationPanel* panel(WizardTouchCalibrationStep& step) {
        return step.panel_.get();
    }

    // The wizard's calibration session — begin_capture/revert_for_retry/restore.
    static helix::TouchCalibrationSession& session(WizardTouchCalibrationStep& step) {
        return step.session_;
    }

    // Inject the calibration sink the retry path drives, so handle_retry_clicked()
    // can be exercised without a live DisplayManager (#943).
    static void set_calibration_sink(WizardTouchCalibrationStep& step,
                                     helix::ICalibrationSink* sink) {
        step.calibration_sink_override_ = sink;
    }

    // Drive the real Retry handler (private in production).
    static void invoke_retry(WizardTouchCalibrationStep& step) {
        step.handle_retry_clicked();
    }
};
