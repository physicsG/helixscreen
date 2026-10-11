// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "calibration_abort.h"

#include "i_moonraker_api.h"

#include <spdlog/spdlog.h>

#include <string>

namespace helix {

void emergency_stop_and_restart(IMoonrakerAPI* api, const char* log_tag) {
    if (!api) {
        spdlog::warn("[{}] emergency stop requested without an API", log_tag);
        return;
    }
    spdlog::info("[{}] Emergency stop: M112 then firmware restart", log_tag);
    const std::string tag = log_tag;
    api->emergency_stop(
        [api, tag]() {
            api->restart_firmware(
                [tag]() { spdlog::debug("[{}] Firmware restart initiated", tag); },
                [tag](const MoonrakerError& err) {
                    spdlog::error("[{}] Firmware restart failed: {}", tag, err.message);
                });
        },
        [tag](const MoonrakerError& err) {
            spdlog::error("[{}] Emergency stop failed: {}", tag, err.message);
        });
}

} // namespace helix
