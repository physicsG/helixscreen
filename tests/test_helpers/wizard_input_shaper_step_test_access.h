// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_wizard_input_shaper.h"

#include <utility>

/// Reaches the wizard input shaper step's live sweep chart: the stream source
/// tests drive, and the chart and preview they read back.
class WizardInputShaperStepTestAccess {
  public:
    static void
    set_preview_source_factory(WizardInputShaperStep& step,
                               helix::calibration::LiveSweepPreview::SourceFactory factory) {
        step.sweep_chart_.set_source_factory(std::move(factory));
    }

    static const helix::ui::ShaperSweepChart& sweep_chart(const WizardInputShaperStep& step) {
        return step.sweep_chart_;
    }
};
