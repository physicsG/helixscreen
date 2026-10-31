// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl/lvgl.h"

#include <optional>

namespace helix::ui {

/// Register `<belt_gap_gauge>`: a horizontal track for the signed gap between
/// the two belt paths' peaks. Draws a matched band of +/- tolerance around
/// zero, a filled needle at this round's gap, a hollow needle at the previous
/// round's and an arrow between them. It draws no text. Size it from XML.
void register_belt_gap_gauge_widget();

/// Feed the gauge. `range_hz` is the gap at the track's ends. Safe at any
/// size, including before layout.
void ui_belt_gap_gauge_set(lv_obj_t* gauge, float gap_hz, std::optional<float> previous_gap_hz,
                           float tolerance_hz, float range_hz);

/// The gap the gauge was last fed, 0 when it never was.
float ui_belt_gap_gauge_get_gap(lv_obj_t* gauge);

/// Track x offset (0 .. track_px-1) for a gap; zero is centred, gaps beyond
/// +/- range_hz clamp to the ends.
int belt_gap_gauge_x(float gap_hz, float range_hz, int track_px);

enum class GapSeverity { MATCHED, NEAR, FAR };

/// MATCHED within tolerance, NEAR up to belt_tune::kGaugeWarnHz, FAR beyond.
GapSeverity belt_gap_severity(float gap_hz, float tolerance_hz);

} // namespace helix::ui
