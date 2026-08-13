// SPDX-License-Identifier: GPL-3.0-or-later

#include "z_offset_utils.h"

#include "ui_emergency_stop.h"
#include "ui_toast_manager.h"

#include "i_moonraker_api.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <lvgl.h>

namespace helix::zoffset {

bool is_auto_saved(ZOffsetCalibrationStrategy strategy) {
    if (strategy == ZOffsetCalibrationStrategy::FIRMWARE_MANAGED) {
        // FIRMWARE_MANAGED means firmware or macros handle Z-offset persistence
        // (e.g., FlashForge firmware, Artillery M1 save-zoffset macro).
        // HelixScreen should not offer its own save path.
        spdlog::debug("[ZOffsetUtils] Z-offset auto-saved by firmware (firmware_managed strategy)");
        ToastManager::instance().show(ToastSeverity::INFO,
                                      lv_tr("Z-offset is auto-saved by firmware"), 3000);
        return true;
    }
    return false;
}

void format_delta(int microns, char* buf, size_t buf_size) {
    if (microns == 0) {
        buf[0] = '\0';
        return;
    }
    double mm = static_cast<double>(microns) / 1000.0;
    std::snprintf(buf, buf_size, "%+.3fmm", mm);
}

void format_offset(int microns, char* buf, size_t buf_size) {
    double mm = static_cast<double>(microns) / 1000.0;
    std::snprintf(buf, buf_size, "%+.3fmm", mm);
}

void format_offset_compact(int microns, char* buf, size_t buf_size) {
    // Drop leading zero for |value| < 1mm: "+.050mm" instead of "+0.050mm"
    int abs_microns = microns < 0 ? -microns : microns;
    if (abs_microns < 1000) {
        char sign = microns < 0 ? '-' : '+';
        std::snprintf(buf, buf_size, "%c.%03dmm", sign, abs_microns);
    } else {
        double mm = static_cast<double>(microns) / 1000.0;
        std::snprintf(buf, buf_size, "%+.3fmm", mm);
    }
}

void apply_and_save(IMoonrakerAPI* api, ZOffsetCalibrationStrategy strategy,
                    std::function<void()> on_success,
                    std::function<void(const std::string& error)> on_error) {
    if (!api) {
        spdlog::error("[ZOffsetUtils] apply_and_save called with null API");
        if (on_error)
            on_error("No printer connection");
        return;
    }

    if (strategy == ZOffsetCalibrationStrategy::FIRMWARE_MANAGED) {
        // Firmware/macros handle persistence — nothing for us to do
        spdlog::debug("[ZOffsetUtils] apply_and_save: firmware_managed strategy — auto-saved");
        if (on_success)
            on_success();
        return;
    }

    const char* apply_cmd = (strategy == ZOffsetCalibrationStrategy::PROBE_CALIBRATE)
                                ? "Z_OFFSET_APPLY_PROBE"
                                : "Z_OFFSET_APPLY_ENDSTOP";

    const char* strategy_name =
        (strategy == ZOffsetCalibrationStrategy::PROBE_CALIBRATE) ? "probe_calibrate" : "endstop";

    spdlog::info("[ZOffsetUtils] Applying Z-offset with {} strategy (cmd: {})", strategy_name,
                 apply_cmd);

    api->execute_gcode(
        apply_cmd,
        [api, apply_cmd, on_success, on_error]() {
            spdlog::info("[ZOffsetUtils] {} success, executing SAVE_CONFIG", apply_cmd);

            // Suppress disconnect modal — SAVE_CONFIG triggers a Klipper restart
            EmergencyStopOverlay::instance().suppress_recovery_dialog(RecoverySuppression::LONG);

            api->execute_gcode(
                "SAVE_CONFIG",
                [on_success]() {
                    spdlog::info("[ZOffsetUtils] SAVE_CONFIG success — Klipper restarting");
                    if (on_success)
                        on_success();
                },
                [on_error](const MoonrakerError& err) {
                    std::string msg = fmt::format(
                        "SAVE_CONFIG failed: {}. Z-offset was applied but not saved. "
                        "Run SAVE_CONFIG manually or the offset will be lost on restart.",
                        err.user_message());
                    spdlog::error("[ZOffsetUtils] {}", msg);
                    if (on_error)
                        on_error(msg);
                });
        },
        [apply_cmd, on_error](const MoonrakerError& err) {
            std::string msg = fmt::format("{} failed: {}", apply_cmd, err.user_message());
            spdlog::error("[ZOffsetUtils] {}", msg);
            if (on_error)
                on_error(msg);
        });
}

bool should_extend_save_timeout(bool restart_latched, unsigned extensions_used,
                                unsigned max_extensions) {
    // Only stall the clock if this save actually triggered a restart. A save
    // that never restarted Klipper and still has not completed is a real hang
    // and must be reported.
    return restart_latched && extensions_used < max_extensions;
}

} // namespace helix::zoffset
