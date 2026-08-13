// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "print_completion.h"

#include "ui_confetti.h"
#include "ui_filename_utils.h"
#include "ui_icon.h"
#include "ui_modal.h"
#include "ui_nav_manager.h"
#include "ui_panel_print_status.h"
#include "ui_toast_manager.h"

#include "app_globals.h"
#include "audio_settings_manager.h"
#include "display_manager.h"
#include "display_settings_manager.h"
#include "format_utils.h"
#include "moonraker_api.h"
#include "moonraker_manager.h"
#include "printer_state.h"
#include "sound_manager.h"
#include "static_subject_registry.h"

#include <spdlog/spdlog.h>

#include <lvgl.h>

using helix::gcode::get_display_filename;
using helix::gcode::resolve_gcode_filename;

namespace helix {

// Track previous state to detect transitions to terminal states
static PrintJobState prev_print_state = PrintJobState::STANDBY;

// Guard against false completion on startup - first update may have stale initial state
static bool has_received_first_update = false;

// --- Subjects for declarative XML bindings in print_completion_modal.xml ---
static bool s_subjects_initialized = false;

// String subjects with backing buffers
static char s_title_buf[64];
static lv_subject_t s_title_subject;

static char s_filename_buf[256];
static lv_subject_t s_filename_subject;

static char s_duration_buf[64];
static lv_subject_t s_duration_subject;

static char s_estimate_buf[64];
static lv_subject_t s_estimate_subject;

static char s_layers_buf[64];
static lv_subject_t s_layers_subject;

static char s_filament_buf[64];
static lv_subject_t s_filament_subject;

// Int subjects for visibility flags (1=visible, 0=hidden)
static lv_subject_t s_has_estimate_subject;
static lv_subject_t s_has_filament_subject;

static void init_completion_subjects() {
    if (s_subjects_initialized)
        return;

    lv_subject_init_string(&s_title_subject, s_title_buf, nullptr, sizeof(s_title_buf), "");
    lv_subject_init_string(&s_filename_subject, s_filename_buf, nullptr, sizeof(s_filename_buf),
                           "");
    lv_subject_init_string(&s_duration_subject, s_duration_buf, nullptr, sizeof(s_duration_buf),
                           "");
    lv_subject_init_string(&s_estimate_subject, s_estimate_buf, nullptr, sizeof(s_estimate_buf),
                           "");
    lv_subject_init_string(&s_layers_subject, s_layers_buf, nullptr, sizeof(s_layers_buf), "");
    lv_subject_init_string(&s_filament_subject, s_filament_buf, nullptr, sizeof(s_filament_buf),
                           "");
    lv_subject_init_int(&s_has_estimate_subject, 1);
    lv_subject_init_int(&s_has_filament_subject, 1);

    lv_xml_register_subject(nullptr, "print_completion_title", &s_title_subject);
    lv_xml_register_subject(nullptr, "print_completion_filename", &s_filename_subject);
    lv_xml_register_subject(nullptr, "print_completion_duration", &s_duration_subject);
    lv_xml_register_subject(nullptr, "print_completion_estimate", &s_estimate_subject);
    lv_xml_register_subject(nullptr, "print_completion_layers", &s_layers_subject);
    lv_xml_register_subject(nullptr, "print_completion_filament", &s_filament_subject);
    lv_xml_register_subject(nullptr, "print_completion_has_estimate", &s_has_estimate_subject);
    lv_xml_register_subject(nullptr, "print_completion_has_filament", &s_has_filament_subject);

    s_subjects_initialized = true;

    StaticSubjectRegistry::instance().register_deinit("PrintCompletion", []() {
        if (!s_subjects_initialized)
            return;
        lv_subject_deinit(&s_title_subject);
        lv_subject_deinit(&s_filename_subject);
        lv_subject_deinit(&s_duration_subject);
        lv_subject_deinit(&s_estimate_subject);
        lv_subject_deinit(&s_layers_subject);
        lv_subject_deinit(&s_filament_subject);
        lv_subject_deinit(&s_has_estimate_subject);
        lv_subject_deinit(&s_has_filament_subject);
        s_subjects_initialized = false;
    });
}

// Helper to cleanup .helix_temp modified G-code files after print ends
static void cleanup_helix_temp_file(const std::string& filename) {
    // Check if this is a .helix_temp modified file
    if (filename.find(".helix_temp/modified_") == std::string::npos) {
        return; // Not a temp file
    }

    auto* mgr = get_moonraker_manager();
    if (!mgr) {
        spdlog::warn("[PrintComplete] Cannot cleanup temp file - MoonrakerManager not available");
        return;
    }

    auto* api = mgr->api();
    if (!api) {
        spdlog::warn("[PrintComplete] Cannot cleanup temp file - API not available");
        return;
    }

    // Moonraker's delete_file requires full path including root
    std::string full_path = "gcodes/" + filename;
    spdlog::info("[PrintComplete] Cleaning up temp file: {}", full_path);

    api->files().delete_file(
        full_path,
        [filename]() { spdlog::info("[PrintComplete] Deleted temp file: {}", filename); },
        [filename](const MoonrakerError& err) {
            // Log but don't show error to user - cleanup is best-effort
            spdlog::warn("[PrintComplete] Failed to delete temp file {}: {}", filename,
                         err.message);
        });
}

// Helper to show the rich print completion modal
static void show_rich_completion_modal(PrintJobState state, const char* filename) {
    init_completion_subjects();

    auto& printer_state = get_printer_state();

    // Get print stats (wall-clock elapsed including prep time)
    int duration_secs = lv_subject_get_int(printer_state.get_print_elapsed_subject());
    int total_layers = lv_subject_get_int(printer_state.get_print_layer_total_subject());
    int estimated_secs = printer_state.get_estimated_print_time();
    int filament_mm = lv_subject_get_int(printer_state.get_print_filament_used_subject());

    spdlog::info("[PrintComplete] Stats: duration={}s, estimated={}s, layers={}, filament={}mm",
                 duration_secs, estimated_secs, total_layers, filament_mm);

    // Determine icon and title based on state
    const char* icon_src = "check_circle";
    const char* icon_variant = "success";
    const char* title = "Print Complete";

    switch (state) {
    case PrintJobState::COMPLETE:
        icon_src = "check_circle";
        icon_variant = "success";
        title = "Print Complete";
        break;
    case PrintJobState::CANCELLED:
        icon_src = "cancel";
        icon_variant = "warning";
        title = "Print Cancelled";
        break;
    case PrintJobState::ERROR:
        icon_src = "alert";
        icon_variant = "error";
        title = "Print Failed";
        break;
    default:
        break;
    }

    // Set all subjects BEFORE showing modal so XML bindings resolve immediately
    lv_subject_copy_string(&s_title_subject, lv_tr(title));
    lv_subject_copy_string(&s_filename_subject, filename);

    std::string duration_str = format::duration_padded(duration_secs) + " " + lv_tr("elapsed");
    lv_subject_copy_string(&s_duration_subject, duration_str.c_str());

    if (estimated_secs > 0) {
        std::string est_str = std::string("est") + " " + format::duration_padded(estimated_secs);
        lv_subject_copy_string(&s_estimate_subject, est_str.c_str());
        lv_subject_set_int(&s_has_estimate_subject, 1);
    } else {
        lv_subject_set_int(&s_has_estimate_subject, 0);
    }

    char layers_tmp[32];
    snprintf(layers_tmp, sizeof(layers_tmp), "%d %s", total_layers, lv_tr("layers"));
    lv_subject_copy_string(&s_layers_subject, layers_tmp);

    if (filament_mm > 0) {
        std::string fil_str =
            format::format_filament_length(static_cast<double>(filament_mm)) + " " + lv_tr("used");
        lv_subject_copy_string(&s_filament_subject, fil_str.c_str());
        lv_subject_set_int(&s_has_filament_subject, 1);
    } else {
        lv_subject_set_int(&s_has_filament_subject, 0);
    }

    // Show modal - XML bind_text and bind_flag_if_eq resolve from subjects above
    lv_obj_t* dialog = helix::ui::modal_show("print_completion_modal");

    if (!dialog) {
        spdlog::error("[PrintComplete] Failed to create print_completion_modal");
        return;
    }

    // Icon src/variant must be set imperatively (no XML binding support for icon properties)
    lv_obj_t* icon_widget = lv_obj_find_by_name(dialog, "status_icon");
    if (icon_widget) {
        ui_icon_set_source(icon_widget, icon_src);
        ui_icon_set_variant(icon_widget, icon_variant);
    }

    // Celebrate successful prints with confetti (respects animations setting)
    if (state == PrintJobState::COMPLETE &&
        DisplaySettingsManager::instance().get_animations_enabled()) {
        lv_obj_t* confetti = ui_confetti_create(lv_layer_top());
        if (confetti) {
            ui_confetti_burst(confetti, 100);
            spdlog::debug("[PrintComplete] Confetti burst for successful print");
        }
    }

    spdlog::info("[PrintComplete] Showing rich completion modal: {} ({})", title, filename);
}

// Callback for print state changes - triggers completion notifications
static void on_print_state_changed_for_notification(lv_observer_t* observer,
                                                    lv_subject_t* subject) {
    (void)observer;
    auto current = static_cast<PrintJobState>(lv_subject_get_int(subject));

    // Skip first callback - state may be stale on startup
    // (Observer initializes prev_print_state before Moonraker updates arrive)
    if (!has_received_first_update) {
        has_received_first_update = true;
        prev_print_state = current; // Initialize to ACTUAL state from Moonraker
        spdlog::debug("[PrintComplete] First update received (state={}), armed for notifications",
                      static_cast<int>(current));
        return;
    }

    spdlog::debug("[PrintComplete] State change: {} -> {}", static_cast<int>(prev_print_state),
                  static_cast<int>(current));

    // Check for transitions to terminal states (from active print states)
    bool was_active =
        (prev_print_state == PrintJobState::PRINTING || prev_print_state == PrintJobState::PAUSED);
    bool is_terminal = (current == PrintJobState::COMPLETE || current == PrintJobState::CANCELLED ||
                        current == PrintJobState::ERROR);

    spdlog::debug("[PrintComplete] was_active={}, is_terminal={}", was_active, is_terminal);

    if (was_active && is_terminal) {
        // Get filename from PrinterState and format for display
        const char* raw_filename =
            lv_subject_get_string(get_printer_state().get_print_filename_subject());
        std::string resolved_filename =
            (raw_filename && raw_filename[0]) ? resolve_gcode_filename(raw_filename) : "";
        std::string display_name =
            !resolved_filename.empty() ? get_display_filename(resolved_filename) : "Unknown";

        // Cleanup temp files - delete .helix_temp/modified_* files after print ends
        // Do this before anything else so it happens regardless of notification settings
        if (raw_filename && raw_filename[0]) {
            cleanup_helix_temp_file(raw_filename);
        }

        // Play sound for terminal state (independent of CompletionAlertMode —
        // sound respects its own sounds_enabled toggle via SoundManager::play())
        switch (current) {
        case PrintJobState::COMPLETE:
            SoundManager::instance().play("print_complete", SoundPriority::EVENT);
            break;
        case PrintJobState::ERROR:
            SoundManager::instance().play("error_alert", SoundPriority::EVENT);
            break;
        case PrintJobState::CANCELLED:
            SoundManager::instance().play("print_cancelled", SoundPriority::EVENT);
            break;
        default:
            break;
        }

        // Check if user is on print status panel
        lv_obj_t* print_status_panel = get_global_print_status_panel().get_panel();
        bool on_print_status = NavigationManager::instance().is_panel_in_stack(print_status_panel);

        auto mode = AudioSettingsManager::instance().get_completion_alert_mode();

        spdlog::debug("[PrintComplete] Print {} - on_print_status={}, mode={}",
                      (current == PrintJobState::COMPLETE)    ? "complete"
                      : (current == PrintJobState::CANCELLED) ? "cancelled"
                                                              : "failed",
                      on_print_status, static_cast<int>(mode));

        // 1. Errors ALWAYS get a modal (high visibility needed)
        if (current == PrintJobState::ERROR) {
            if (auto* dm = DisplayManager::instance()) {
                dm->wake_display();
            }

            // Proactively turn off heaters on print error to prevent
            // indefinite heating (Klipper doesn't auto-off on error)
            if (auto* mgr = get_moonraker_manager()) {
                if (auto* client = mgr->client()) {
                    spdlog::info("[PrintComplete] Turning off heaters after print error");
                    client->gcode_script("TURN_OFF_HEATERS");
                }
            }

            show_rich_completion_modal(current, display_name.c_str());
            prev_print_state = current;
            return;
        }

        // 2. On print status panel - no notification needed (panel shows state)
        if (on_print_status) {
            spdlog::debug("[PrintComplete] On print status panel - skipping notification");
            prev_print_state = current;
            return;
        }

        // 3. On other panels - respect the completion alert mode setting
        switch (mode) {
        case CompletionAlertMode::OFF:
            spdlog::debug("[PrintComplete] Notification disabled by setting");
            break;

        case CompletionAlertMode::NOTIFICATION: {
            if (auto* dm = DisplayManager::instance()) {
                dm->wake_display();
            }
            char message[128];
            ToastSeverity severity = (current == PrintJobState::COMPLETE) ? ToastSeverity::SUCCESS
                                                                          : ToastSeverity::WARNING;
            snprintf(message, sizeof(message), "Print %s: %s",
                     (current == PrintJobState::COMPLETE) ? "complete" : "cancelled",
                     display_name.c_str());
            ToastManager::instance().show(severity, message, 5000);
            break;
        }

        case CompletionAlertMode::ALERT:
            if (auto* dm = DisplayManager::instance()) {
                dm->wake_display();
            }
            show_rich_completion_modal(current, display_name.c_str());
            break;
        }
    }

    prev_print_state = current;
}

ObserverGuard init_print_completion_observer() {
    // Initialize subjects early so XML bindings are available when modal is shown
    init_completion_subjects();

    // Reset state tracking on (re)initialization
    // prev_print_state will be set to actual state on first callback
    has_received_first_update = false;
    prev_print_state = PrintJobState::STANDBY;

    spdlog::debug("[PrintComplete] Observer registered, awaiting first Moonraker update");
    return ObserverGuard(get_printer_state().get_print_state_enum_subject(),
                         on_print_state_changed_for_notification, nullptr);
}

void cleanup_stale_helix_temp_files(IMoonrakerAPI* api) {
    if (!api) {
        spdlog::warn("[PrintComplete] Cannot cleanup stale temp files - API not available");
        return;
    }

    // List files in .helix_temp directory
    // Note: Moonraker returns ALL files in root, not just the path we request
    // We must filter by path prefix ourselves
    api->files().list_files(
        "gcodes", ".helix_temp", false,
        [api](const std::vector<FileInfo>& files) {
            // Filter to only files actually in .helix_temp/
            int cleanup_count = 0;
            for (const auto& file : files) {
                if (file.is_dir) {
                    continue;
                }

                // Only process files that are actually in .helix_temp/
                // file.path contains the relative path from root (e.g.,
                // ".helix_temp/modified_123.gcode")
                if (file.path.find(".helix_temp/") != 0) {
                    continue; // Not in .helix_temp directory
                }

                cleanup_count++;

                // Moonraker's delete_file requires full path including root
                std::string filepath = "gcodes/" + file.path;
                api->files().delete_file(
                    filepath,
                    [filepath]() {
                        spdlog::debug("[PrintComplete] Deleted stale temp file: {}", filepath);
                    },
                    [filepath](const MoonrakerError& err) {
                        spdlog::warn("[PrintComplete] Failed to delete stale temp file {}: {}",
                                     filepath, err.message);
                    });
            }

            if (cleanup_count > 0) {
                spdlog::info("[PrintComplete] Cleaning up {} stale temp files from .helix_temp",
                             cleanup_count);
            } else {
                spdlog::debug("[PrintComplete] No stale temp files to clean up");
            }
        },
        [](const MoonrakerError& err) {
            // Directory doesn't exist or can't be listed - that's fine
            spdlog::debug("[PrintComplete] No .helix_temp directory to clean: {}", err.message);
        });
}

} // namespace helix
