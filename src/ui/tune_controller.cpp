// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tune_controller.h"

#include "ui_error_reporting.h"

#include "i_moonraker_api.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <string>

namespace helix::tune {

int clamp_speed_percent(int pct) {
    return std::clamp(pct, kSpeedMinPct, kSpeedMaxPct);
}

int clamp_flow_percent(int pct) {
    return std::clamp(pct, kFlowMinPct, kFlowMaxPct);
}

void set_speed_percent(IMoonrakerAPI* api, int pct) {
    if (!api) {
        return;
    }
    const int value = clamp_speed_percent(pct);
    api->execute_gcode(
        "M220 S" + std::to_string(value),
        [value]() { spdlog::debug("[TuneController] Speed set to {}%", value); },
        [](const MoonrakerError& err) {
            spdlog::error("[TuneController] Failed to set speed: {}", err.message);
            NOTIFY_ERROR(lv_tr("Failed to set print speed: {}"), err.user_message());
        });
}

void set_flow_percent(IMoonrakerAPI* api, int pct) {
    if (!api) {
        return;
    }
    const int value = clamp_flow_percent(pct);
    api->execute_gcode(
        "M221 S" + std::to_string(value),
        [value]() { spdlog::debug("[TuneController] Flow set to {}%", value); },
        [](const MoonrakerError& err) {
            spdlog::error("[TuneController] Failed to set flow: {}", err.message);
            NOTIFY_ERROR(lv_tr("Failed to set flow rate: {}"), err.user_message());
        });
}

} // namespace helix::tune
