// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_print_start_controller.cpp
 * @brief Controller for print initiation workflow
 *
 * Extracted from ui_panel_print_select.cpp to separate print start concerns.
 */

#include "ui_print_start_controller.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_filament_mapping_card.h"
#include "ui_modal.h"
#include "ui_nav_manager.h"
#include "ui_panel_print_status.h"
#include "ui_print_select_detail_view.h"
#include "ui_update_queue.h"

#include "active_print_media_manager.h"
#include "ams_state.h"
#include "app_constants.h"
#include "color_utils.h"
#include "data_root_resolver.h"
#include "filament_database.h"
#include "filament_sensor_manager.h"
#include "helix_psram_attr.h"
#include "i_moonraker_api.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "observer_factory.h"
#include "printer_state.h"
#include "settings_manager.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "hv/json.hpp"

namespace helix::ui {

// ============================================================================
// Constructor / Destructor
// ============================================================================

PrintStartController::PrintStartController(PrinterState& printer_state, IMoonrakerAPI* api)
    : printer_state_(printer_state), api_(api) {
    spdlog::debug("[PrintStartController] Created");
}

PrintStartController::~PrintStartController() {
    // Warn if we still have a pending remap restore — the mapping will NOT be
    // restored automatically since the observer is about to be destroyed.
    if (!saved_tool_mapping_.empty()) {
        spdlog::warn("[PrintStartController] Destroyed with pending remap restore "
                     "({} tools on backend {}) — mapping will not be restored",
                     saved_tool_mapping_.size(), saved_backend_index_);
    }

    // Clear observers before modal cleanup. All three must be reset here, not
    // just the print-state one: each holds a context pointing at this object,
    // and the klippy/AMS guards stay armed across a whole deferred-restore wait
    // (ObserverGuard, never release() — #579).
    print_state_observer_.reset();
    klippy_state_observer_.reset();
    ams_data_observer_.reset();

    // Clean up any open modals - only if LVGL is still initialized
    // (destructor may be called after lv_deinit() during shutdown)
    if (lv_is_initialized()) {
        if (filament_warning_modal_) {
            helix::ui::modal_hide(filament_warning_modal_);
            filament_warning_modal_ = nullptr;
        }
        if (color_mismatch_modal_) {
            helix::ui::modal_hide(color_mismatch_modal_);
            color_mismatch_modal_ = nullptr;
        }
        if (material_mismatch_modal_) {
            helix::ui::modal_hide(material_mismatch_modal_);
            material_mismatch_modal_ = nullptr;
        }
        if (insufficient_filament_modal_) {
            helix::ui::modal_hide(insufficient_filament_modal_);
            insufficient_filament_modal_ = nullptr;
        }
    }
    spdlog::trace("[PrintStartController] Destroyed");
}

// ============================================================================
// Setup
// ============================================================================

void PrintStartController::set_api(IMoonrakerAPI* api) {
    api_ = api;
}

void PrintStartController::set_detail_view(PrintSelectDetailView* detail_view) {
    detail_view_ = detail_view;
}

void PrintStartController::set_file(const std::string& filename, const std::string& path,
                                    const std::vector<std::string>& filament_colors,
                                    const std::string& thumbnail_path) {
    filename_ = filename;
    path_ = path;
    filament_colors_ = filament_colors;
    thumbnail_path_ = thumbnail_path;
}

bool PrintStartController::is_ready() const {
    return !filename_.empty() && detail_view_ != nullptr;
}

// ============================================================================
// Print Initiation
// ============================================================================

void PrintStartController::initiate() {
    // Safety: reject print starts during startup grace period.
    // Ghost touch events during app initialization can trigger the full print chain
    // (file select → detail view → print button → filament warning confirm) in seconds.
    auto elapsed = std::chrono::steady_clock::now() - AppConstants::Startup::PROCESS_START_TIME;
    if (elapsed < AppConstants::Startup::PRINT_START_GRACE_PERIOD) {
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        spdlog::warn("[PrintStartController] Rejected print start during startup grace period "
                     "({}s < {}s)",
                     secs, AppConstants::Startup::PRINT_START_GRACE_PERIOD.count());
        if (update_print_button_) {
            update_print_button_(); // Re-enable button
        }
        return;
    }

    // OPTIMISTIC UI: Disable button IMMEDIATELY to prevent double-clicks.
    // This must happen BEFORE any async work or checks that could allow
    // the user to click again while we're processing.
    if (can_print_subject_) {
        lv_subject_set_int(can_print_subject_, 0);
    }

    // Check if a print is already active before allowing a new one to start
    if (!printer_state_.can_start_new_print()) {
        PrintJobState current_state = printer_state_.get_print_job_state();
        const char* state_str = print_job_state_to_string(current_state);
        NOTIFY_ERROR(lv_tr("Cannot start print: printer is {}"), state_str);
        spdlog::warn("[PrintStartController] Attempted to start print while printer is in {} state",
                     state_str);
        if (update_print_button_) {
            update_print_button_(); // Re-enable button on early failure
        }
        return;
    }

    // Check if assigned external spool has enough filament for the print.
    if (auto spool = AmsState::instance().get_external_spool_info();
        spool.has_value() && spool->remaining_weight_g > 0.0f) {
        auto metadata = detail_view_ ? detail_view_->get_file_metadata() : std::nullopt;
        if (metadata.has_value()) {
            float needed_g = static_cast<float>(metadata->filament_weight_total);
            if (needed_g <= 0.0f && metadata->filament_total > 0.0) {
                // Fall back to length-based estimate using the spool's material.
                auto mat = filament::find_material(spool->material);
                if (mat.has_value() && mat->density_g_cm3 > 0.0f) {
                    needed_g = filament::length_to_weight_g(
                        static_cast<float>(metadata->filament_total), mat->density_g_cm3, 1.75f);
                }
            }
            if (needed_g > 0.0f && needed_g > spool->remaining_weight_g) {
                spdlog::info("[PrintStartController] Pre-print warning: needs {} g, spool has {} g",
                             needed_g, spool->remaining_weight_g);
                show_insufficient_filament_warning(needed_g, spool->remaining_weight_g);
                return;
            }
        }
    }

    // Pre-print filament-present check (auto-unload suppression + AMS lane truth
    // / non-AMS aggregate fallback). Shared with the insufficient-filament
    // continuation so both paths behave identically. Returns true if a warning
    // dialog was shown — the dialog drives continuation, so we return here.
    if (check_required_filament_present()) {
        return;
    }

    // Check if any tools have no matching AMS slot (uses FilamentMapper results)
    auto unresolved = find_unresolved_tools();
    if (!unresolved.empty()) {
        auto tool_info = detail_view_->get_filament_tool_info();
        show_color_mismatch_warning(unresolved, tool_info);
        return;
    }

    // Check material compatibility (both AMS and non-AMS)
    continue_after_unresolved_check();
}

void PrintStartController::execute_print_start() {
    // OPTIMISTIC UI: Disable button immediately to prevent double-clicks
    if (can_print_subject_) {
        lv_subject_set_int(can_print_subject_, 0);
    }

    auto* prep_manager = detail_view_ ? detail_view_->get_prep_manager() : nullptr;
    if (!prep_manager) {
        spdlog::error("[PrintStartController] Cannot start print - prep manager not initialized");
        NOTIFY_ERROR(lv_tr("Cannot start print: internal error"));
        if (update_print_button_) {
            update_print_button_(); // Re-enable button on early failure
        }
        return;
    }

    std::string filename_to_print = filename_;

    // Read the selected pre-print options for the start log below. Applying
    // them (timelapse included) is owned entirely by prep_manager->start_print().
    auto options = prep_manager->read_options_from_subjects();

    spdlog::info(
        "[PrintStartController] Starting print: {} (pre-print: mesh={}, qgl={}, z_tilt={}, "
        "clean={}, timelapse={})",
        filename_to_print, options.bed_mesh, options.qgl, options.z_tilt, options.nozzle_clean,
        options.timelapse);

    // Apply filament remaps if user changed any mappings
    bool remaps_applied = apply_filament_remaps();
    if (remaps_applied) {
        observe_print_state_for_restore();
    }

    // Timelapse application now lives SOLELY in
    // PrintPreparationManager::start_print(), which dispatches the timelapse
    // RuntimeCommand in BOTH directions (on/off). The redundant enable-only
    // write that used to live here caused a double write on print start (#1094).

    // Capture callbacks for use in lambdas
    auto on_started = on_print_started_;
    auto on_cancelled = on_print_cancelled_;
    auto update_button = update_print_button_;
    auto show_detail = show_detail_view_;

    // Capture thumbnail path for lambda
    std::string thumbnail_path = thumbnail_path_;

    // The actual "start the print" step: navigate to the status panel, then
    // delegate to PrintPreparationManager. Hoisted into a continuation so the
    // Snapmaker U1 native pre-print config send (which must land BEFORE
    // PRINT_START) can run first and only invoke this on success.
    auto start_now = [this, prep_manager, filename_to_print, path = path_, thumbnail_path,
                      on_started, update_button, show_detail]() {
        // Navigate to print status panel IMMEDIATELY (optimistic navigation)
        // The busy overlay will show on top during download/upload operations.
        // On failure, we'll navigate back to the detail overlay.
        if (navigate_to_print_status_) {
            spdlog::info("[PrintStartController] Navigating to print status panel (preparing...)");
            if (hide_detail_view_) {
                hide_detail_view_();
            }
            navigate_to_print_status_();
        }

        // Delegate to PrintPreparationManager
        prep_manager->start_print(
            filename_to_print, path,
            // Navigation callback - called when Moonraker confirms print start
            // Sets thumbnail source so PrintStatusPanel loads the correct thumbnail
            // NOTE: Called from background HTTP thread - must defer LVGL calls to main thread
            [filename_to_print, path, thumbnail_path, on_started]() {
                // Construct full path for metadata lookup (e.g., usb/flowrate_0.gcode)
                std::string full_path =
                    path.empty() ? filename_to_print : path + "/" + filename_to_print;
                helix::ui::queue_update([full_path, thumbnail_path, on_started]() {
                    auto& status_panel = get_global_print_status_panel();
                    status_panel.set_thumbnail_source(full_path);

                    // If we have a pre-extracted thumbnail (USB/embedded), set it directly
                    // This bypasses Moonraker metadata lookup which doesn't have USB file info
                    if (!thumbnail_path.empty()) {
                        // Tag it with the same identity handed to
                        // set_thumbnail_source() above: the manager cannot infer
                        // it here, since Moonraker may not have reported the
                        // print filename yet.
                        helix::get_active_print_media_manager().set_thumbnail_path(full_path,
                                                                                   thumbnail_path);
                        spdlog::debug("[PrintStartController] Set extracted thumbnail path: {}",
                                      thumbnail_path);
                    }

                    spdlog::debug(
                        "[PrintStartController] Print start confirmed, thumbnail source set: {}",
                        full_path);
                    if (on_started) {
                        on_started();
                    }
                });
            },
            // Completion callback
            // NOTE: Called from background HTTP thread - must defer LVGL calls to main thread
            [update_button, show_detail](bool success, const std::string& error) {
                helix::ui::queue_update([success, error, update_button, show_detail]() {
                    auto& status_panel = get_global_print_status_panel();

                    if (success) {
                        spdlog::debug("[PrintStartController] Print started successfully");
                        status_panel.end_preparing(true);
                    } else if (!error.empty()) {
                        NOTIFY_ERROR(lv_tr("Print preparation failed: {}"), error);
                        LOG_ERROR_INTERNAL("[PrintStartController] Print preparation failed: {}",
                                           error);
                        status_panel.end_preparing(false);

                        // Only unwind the optimistic navigation if the print
                        // status overlay is still what the user is looking at.
                        // Preparation can fail a full minute after we pushed it
                        // (upload/prep timeout), by which point they may have
                        // navigated several overlays on — popping blind then
                        // closes THEIR screen, and the re-show below rebuilds
                        // widgets that same pop just destroyed (#1221).
                        // get_cached_overlay() is the widget push_overlay()
                        // actually put on the stack, and it goes null with
                        // destroy-on-close — the same handle the auto-nav gate
                        // checks membership with (print_start_navigation.cpp).
                        auto& nav = NavigationManager::instance();
                        if (nav.is_panel_on_top(PrintStatusPanel::get_cached_overlay())) {
                            spdlog::info("[PrintStartController] Navigating back to print select "
                                         "after failure");
                            nav.go_back(); // Pop print status overlay

                            // Re-show the detail view so user can retry
                            if (show_detail) {
                                show_detail();
                            }
                        } else {
                            spdlog::info("[PrintStartController] Print status no longer on top — "
                                         "leaving navigation alone after failure");
                        }

                        // Re-enable button on failure
                        if (update_button) {
                            update_button();
                        }
                    }
                });
            });
    };

    // Some backends (Snapmaker U1) must emit firmware-native print_task_config
    // gcode BEFORE the print starts (it errors mid-print). ALWAYS-ON for U1 —
    // even with no remap — because SET_PRINT_USED_EXTRUDERS suppresses a
    // spurious-feed runout (the slicer auto-feeds heads the print doesn't use →
    // empty head → runout cancel). Only backends that advertise
    // requires_preprint_send() trigger the pre-send; every other backend takes
    // the unchanged synchronous start path below.
    if (AmsBackend* backend = AmsState::instance().get_backend();
        backend && backend->requires_preprint_send()) {
        send_snapmaker_preprint_then(
            detail_view_ ? detail_view_->get_tools_used() : std::set<int>{},
            detail_view_ ? detail_view_->get_effective_remap() : std::map<int, int>{}, start_now,
            [this]() {
                if (update_print_button_) {
                    update_print_button_();
                }
                if (on_print_cancelled_) {
                    on_print_cancelled_();
                }
            });
        return; // start_now fires from the send continuation — do NOT also start here
    }

    start_now();
}

void PrintStartController::send_snapmaker_preprint_then(const std::set<int>& tools_used,
                                                        const std::map<int, int>& remap,
                                                        std::function<void()> on_done,
                                                        std::function<void()> on_abort) {
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        // No backend — nothing to pre-send; proceed with the start step.
        if (on_done) {
            on_done();
        }
        return;
    }

    std::string gcode = backend->build_preprint_gcode(tools_used, remap);

    if (gcode.empty()) {
        // Nothing the firmware needs (e.g. no parsed tools) — start immediately.
        spdlog::debug("[PrintStartController] U1 pre-print config empty — starting print directly");
        if (on_done) {
            on_done();
        }
        return;
    }

    spdlog::info("[PrintStartController] U1 pre-print config:\n{}", gcode);

    if (!api_) {
        spdlog::error("[PrintStartController] No API to send U1 pre-print config — aborting print");
        NOTIFY_ERROR(lv_tr("Print setup failed: internal error"));
        if (on_abort) {
            on_abort();
        }
        return;
    }

    auto tok = lifetime_.token();
    api_->execute_gcode(
        gcode,
        // Success: defer to main thread, then run the real start step.
        [tok, on_done]() mutable {
            tok.defer("PrintStartController::preprint.ok", [on_done]() {
                if (on_done) {
                    on_done();
                }
            });
        },
        // Error: defer to main thread, surface a user-visible failure, and ABORT.
        // Better to fail loud than feed an empty head.
        [tok, on_abort](const MoonrakerError& err) mutable {
            std::string msg = err.message;
            tok.defer("PrintStartController::preprint.err", [msg, on_abort]() {
                LOG_ERROR_INTERNAL("[PrintStartController] U1 pre-print config rejected: {}", msg);
                NOTIFY_ERROR_MODAL(
                    lv_tr("Print setup failed"), "{}",
                    lv_tr("The printer rejected the filament configuration. The print was not "
                          "started."));
                // Notify caller to re-enable UI state — do NOT start.
                if (on_abort) {
                    on_abort();
                }
            });
        },
        15000);
}

void PrintStartController::initiate_reprint(const std::string& filename, const std::string& path,
                                            const std::set<int>& tools_used,
                                            std::function<void()> on_started,
                                            std::function<void()> on_error) {
    (void)path; // The lightweight start uses the bare filename; path kept for symmetry.

    if (!api_) {
        spdlog::error("[PrintStartController] initiate_reprint: no API");
        if (on_error) {
            on_error();
        }
        return;
    }

    // Lightweight start — the file is already on the printer; no upload/prep.
    auto start = [this, filename, on_started, on_error]() {
        auto tok = lifetime_.token();
        api_->job().start_print(
            filename,
            [tok, on_started]() mutable {
                tok.defer("PrintStartController::reprint.ok", [on_started]() {
                    if (on_started) {
                        on_started();
                    }
                });
            },
            [tok, on_error](const MoonrakerError& err) mutable {
                std::string msg = err.user_message();
                tok.defer("PrintStartController::reprint.err", [msg, on_error]() {
                    NOTIFY_ERROR(lv_tr("Failed to reprint: {}"), msg);
                    if (on_error) {
                        on_error();
                    }
                });
            });
    };

    // Backends that need a pre-print send (Snapmaker U1) emit the firmware-native
    // print_task_config gcode BEFORE the reprint starts, same as the normal start
    // path. remap is empty (no reprint remap UI) → identity, which reproduces the
    // spurious-feed fix. On native-send error the modal is shown and on_error runs.
    AmsBackend* backend = AmsState::instance().get_backend();
    if (backend && backend->requires_preprint_send()) {
        send_snapmaker_preprint_then(tools_used, /*remap=*/{}, start, on_error);
        return; // start fires from the send continuation — do NOT also start here
    }

    start();
}

// ============================================================================
// Filament Warning Dialog
// ============================================================================

std::vector<std::pair<int, int>> PrintStartController::find_empty_required_lanes() {
    if (!detail_view_) {
        return {};
    }
    return helix::FilamentSensorManager::instance().find_empty_required_lanes(
        detail_view_->get_tools_used(), detail_view_->get_effective_remap());
}

std::string PrintStartController::build_empty_lane_message(
    const std::vector<std::pair<int, int>>& empty) const {
    // Name the offending tool(s) and the AMS lane each routes to so the user
    // knows exactly which lane to load. Lane numbers are 1-based for display
    // (slot 0 -> "Lane 1") to match the rest of the slot UI.
    std::string message;
    if (empty.size() == 1) {
        message = fmt::format(lv_tr("Tool {} → Lane {}: no filament loaded."), empty[0].first,
                              empty[0].second + 1);
    } else {
        message = lv_tr("These tools have no filament loaded:");
        message += "\n\n";
        for (const auto& [tool, slot] : empty) {
            message += fmt::format("  {} {} {} → {} {}\n", LV_SYMBOL_BULLET, lv_tr("Tool"), tool,
                                   lv_tr("Lane"), slot + 1);
        }
    }
    message += "\n\n";
    message += lv_tr("Start print anyway?");
    return message;
}

bool PrintStartController::check_required_filament_present() {
    auto& sensor_mgr = helix::FilamentSensorManager::instance();
    auto& ams_state = AmsState::instance();

    // Backends that auto-unload the toolhead after each print (e.g. AD5X IFS)
    // leave the extruder empty by design, so a "no filament" reading at
    // print-start is expected and the warning is noise. ANY such backend
    // suppresses the warning (matches initiate()'s original intent).
    bool suppress_runout_warning = false;
    bool ams_manages_filament = false;
    for (int i = 0; i < ams_state.backend_count(); ++i) {
        if (auto* backend = ams_state.get_backend(i)) {
            ams_manages_filament = true;
            if (backend->auto_unloads_after_print()) {
                suppress_runout_warning = true;
            }
        }
    }
    if (suppress_runout_warning) {
        return false;
    }

    // AMS lane-truth path: scope the check to the tools the print actually uses
    // and consult the backend's authoritative per-slot presence (Snapmaker U1:
    // print_task_config.filament_exist[head]) instead of the aggregate motion
    // sensors. Avoids two false positives: a used head whose filament is staged
    // in the lane but retracted from the toolhead, and unused empty heads. The
    // lane scan is scoped to the ACTIVE backend (index 0), matching
    // FilamentSensorManager's per-backend slot indexing.
    if (ams_manages_filament && ams_state.get_backend()) {
        auto empty = find_empty_required_lanes();
        if (!empty.empty()) {
            spdlog::info("[PrintStartController] {} required tool(s) have an empty lane - "
                         "showing pre-print warning",
                         empty.size());
            show_filament_warning(build_empty_lane_message(empty));
            return true;
        }
        return false;
    }

    // Non-AMS / no-active-backend fallback: aggregate runout-sensor check (unchanged).
    if (sensor_mgr.is_master_enabled() &&
        sensor_mgr.is_sensor_available(helix::FilamentSensorRole::RUNOUT) &&
        !sensor_mgr.is_filament_detected(helix::FilamentSensorRole::RUNOUT)) {
        spdlog::info("[PrintStartController] Runout sensor shows no filament - showing pre-print "
                     "warning");
        show_filament_warning();
        return true;
    }

    return false;
}

void PrintStartController::show_filament_warning(const std::string& message) {
    // Close any existing dialog first
    if (filament_warning_modal_) {
        helix::ui::modal_hide(filament_warning_modal_);
        filament_warning_modal_ = nullptr;
    }

    // modal_show_confirmation copies the message string into the dialog's label
    // (via the XML attribute path), so pass it directly — no static buffer, no
    // truncation of long multi-tool / translated messages.
    const char* body = message.empty() ? lv_tr("The runout sensor indicates no filament is loaded. "
                                               "Start print anyway?")
                                       : message.c_str();

    filament_warning_modal_ = helix::ui::modal_show_confirmation(
        lv_tr("No Filament Detected"), body, ModalSeverity::Warning, lv_tr("Start Print"),
        on_filament_warning_proceed_static, on_filament_warning_cancel_static, this);

    if (!filament_warning_modal_) {
        spdlog::error("[PrintStartController] Failed to create filament warning dialog");
        // Re-enable print button since we couldn't show the dialog
        if (update_print_button_) {
            update_print_button_();
        }
        return;
    }

    spdlog::debug("[PrintStartController] Pre-print filament warning dialog shown");
}

void PrintStartController::on_filament_warning_proceed_static(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStartController] on_filament_warning_proceed_static");
    auto* self = static_cast<PrintStartController*>(lv_event_get_user_data(e));
    if (self) {
        // Hide dialog first
        if (self->filament_warning_modal_) {
            helix::ui::modal_hide(self->filament_warning_modal_);
            self->filament_warning_modal_ = nullptr;
        }

        // Continue with remaining checks (unresolved tools, material mismatch)
        auto unresolved = self->find_unresolved_tools();
        if (!unresolved.empty()) {
            auto tool_info = self->detail_view_->get_filament_tool_info();
            self->show_color_mismatch_warning(unresolved, tool_info);
            return;
        }
        self->continue_after_unresolved_check();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStartController::on_filament_warning_cancel_static(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStartController] on_filament_warning_cancel_static");
    auto* self = static_cast<PrintStartController*>(lv_event_get_user_data(e));
    if (self) {
        if (self->filament_warning_modal_) {
            helix::ui::modal_hide(self->filament_warning_modal_);
            self->filament_warning_modal_ = nullptr;
        }
        // Re-enable print button since user cancelled
        if (self->update_print_button_) {
            self->update_print_button_();
        }
        if (self->on_print_cancelled_) {
            self->on_print_cancelled_();
        }
        spdlog::debug("[PrintStartController] Print cancelled by user (no filament warning)");
    }
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// Insufficient Filament Warning Dialog
// ============================================================================

void PrintStartController::show_insufficient_filament_warning(float needed_g, float remaining_g) {
    if (insufficient_filament_modal_) {
        helix::ui::modal_hide(insufficient_filament_modal_);
        insufficient_filament_modal_ = nullptr;
    }

    char body[256];
    std::snprintf(body, sizeof(body),
                  lv_tr("This print needs about %.0fg but the spool has about %.0fg "
                        "remaining. Start anyway?"),
                  needed_g, remaining_g);

    insufficient_filament_modal_ = helix::ui::modal_show_confirmation(
        lv_tr("Not Enough Filament"), body, ModalSeverity::Warning, lv_tr("Start Anyway"),
        on_insufficient_filament_proceed_static, on_insufficient_filament_cancel_static, this);

    if (!insufficient_filament_modal_) {
        spdlog::error("[PrintStartController] Failed to create insufficient-filament dialog");
        if (update_print_button_) {
            update_print_button_();
        }
    }
}

void PrintStartController::on_insufficient_filament_proceed_static(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStartController] on_insufficient_filament_proceed_static");
    auto* self = static_cast<PrintStartController*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }
    if (self->insufficient_filament_modal_) {
        helix::ui::modal_hide(self->insufficient_filament_modal_);
        self->insufficient_filament_modal_ = nullptr;
    }

    // Continue the existing pre-print chain: filament-present check next. Uses
    // the SAME shared gate as initiate() so the auto-unload suppression (AD5X
    // IFS) and AMS lane truth apply identically here.
    if (self->check_required_filament_present()) {
        return;
    }
    auto unresolved = self->find_unresolved_tools();
    if (!unresolved.empty()) {
        auto tool_info = self->detail_view_->get_filament_tool_info();
        self->show_color_mismatch_warning(unresolved, tool_info);
        return;
    }
    self->continue_after_unresolved_check();
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStartController::on_insufficient_filament_cancel_static(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStartController] on_insufficient_filament_cancel_static");
    auto* self = static_cast<PrintStartController*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }
    if (self->insufficient_filament_modal_) {
        helix::ui::modal_hide(self->insufficient_filament_modal_);
        self->insufficient_filament_modal_ = nullptr;
    }
    if (self->update_print_button_) {
        self->update_print_button_();
    }
    if (self->on_print_cancelled_) {
        self->on_print_cancelled_();
    }
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// Filament Mismatch Detection
// ============================================================================

std::vector<int> PrintStartController::find_unresolved_tools() {
    // Skip check for single-color prints (no mapping needed)
    if (filament_colors_.size() <= 1) {
        return {};
    }

    // Use the already-computed FilamentMapper results from the mapping card
    if (!detail_view_) {
        spdlog::debug("[PrintStartController] No detail view, skipping mismatch check");
        return {};
    }

    auto mappings = detail_view_->get_filament_mappings();
    if (mappings.empty()) {
        // No mappings = AMS not available or card not shown
        spdlog::debug("[PrintStartController] No filament mappings available");
        return {};
    }

    auto unresolved = helix::FilamentMapper::find_unresolved_tools(mappings);
    if (!unresolved.empty()) {
        spdlog::info("[PrintStartController] {} tools have no matching AMS slot",
                     unresolved.size());
    }
    return unresolved;
}

void PrintStartController::show_color_mismatch_warning(
    const std::vector<int>& unresolved_tools, const std::vector<helix::GcodeToolInfo>& tool_info) {
    // Close any existing dialog first
    if (color_mismatch_modal_) {
        helix::ui::modal_hide(color_mismatch_modal_);
        color_mismatch_modal_ = nullptr;
    }

    // Build message with human-readable color names
    std::string message = lv_tr("These tools have no matching filament loaded:");
    message += "\n\n";
    for (int tool_idx : unresolved_tools) {
        // Look up by real tool_index — tool_info may be used-filtered (compacted),
        // so its vector position no longer equals the tool number.
        const auto* tool = helix::ui::FilamentMappingCard::find_by_tool_index(tool_info, tool_idx);
        if (tool) {
            std::string color_name = helix::describe_color(tool->color_rgb);
            message += "  " + std::string(LV_SYMBOL_BULLET) + " T" + std::to_string(tool_idx) +
                       ": " + color_name;
            if (!tool->material.empty()) {
                message += " (" + tool->material + ")";
            }
            message += "\n";
        }
    }
    message += "\n";
    message += lv_tr("Load the required filaments or start anyway?");

    // Static buffer for message - must persist during modal lifetime (modal stores pointer).
    // Safe because we always close any existing dialog first above,
    // preventing concurrent access to this buffer.
    static HELIX_PSRAM_BSS char message_buffer[1024];
    snprintf(message_buffer, sizeof(message_buffer), "%s", message.c_str());

    color_mismatch_modal_ = helix::ui::modal_show_confirmation(
        lv_tr("Color Mismatch"), message_buffer, ModalSeverity::Warning, lv_tr("Start Anyway"),
        on_color_mismatch_proceed_static, on_color_mismatch_cancel_static, this);

    if (!color_mismatch_modal_) {
        spdlog::error("[PrintStartController] Failed to create color mismatch warning dialog");
        if (update_print_button_) {
            update_print_button_();
        }
        return;
    }

    spdlog::debug("[PrintStartController] Color mismatch warning shown for {} tools",
                  unresolved_tools.size());
}

void PrintStartController::on_color_mismatch_proceed_static(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStartController] on_color_mismatch_proceed_static");
    auto* self = static_cast<PrintStartController*>(lv_event_get_user_data(e));
    if (self) {
        // Hide dialog first
        if (self->color_mismatch_modal_) {
            helix::ui::modal_hide(self->color_mismatch_modal_);
            self->color_mismatch_modal_ = nullptr;
        }
        // Continue to material compatibility check before executing
        self->continue_after_unresolved_check();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStartController::on_color_mismatch_cancel_static(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStartController] on_color_mismatch_cancel_static");
    auto* self = static_cast<PrintStartController*>(lv_event_get_user_data(e));
    if (self) {
        if (self->color_mismatch_modal_) {
            helix::ui::modal_hide(self->color_mismatch_modal_);
            self->color_mismatch_modal_ = nullptr;
        }
        // Re-enable print button since user cancelled
        if (self->update_print_button_) {
            self->update_print_button_();
        }
        if (self->on_print_cancelled_) {
            self->on_print_cancelled_();
        }
        spdlog::debug("[PrintStartController] Print cancelled by user (color mismatch warning)");
    }
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// Material Mismatch Detection
// ============================================================================

void PrintStartController::continue_after_unresolved_check() {
    auto mismatches = find_material_mismatches();
    if (!mismatches.empty()) {
        show_material_mismatch_warning(mismatches);
        return;
    }

    execute_print_start();
}

std::vector<PrintStartController::MaterialMismatchDetail>
PrintStartController::find_material_mismatches() {
    std::vector<MaterialMismatchDetail> mismatches;

    if (!detail_view_) {
        return mismatches;
    }

    // Per-tool filament weights from gcode metadata. Used to skip tools the
    // slicer assigned a material to but that never actually extrude (common
    // when a multi-tool profile prints a single-tool file: T0..T2 inherit the
    // profile's defaults but only T3 is used). Empty vector means the slicer
    // didn't emit per-tool data — in that case we keep the old behavior and
    // check every tool. Graceful fallback, no false-negatives possible.
    std::vector<double> filament_weights;
    if (auto md = detail_view_->get_file_metadata()) {
        filament_weights = md->filament_weights;
    }
    auto tool_is_used = [&filament_weights](int tool_index) -> bool {
        if (filament_weights.empty()) {
            return true; // No data → check everything (old behavior).
        }
        if (tool_index < 0 || tool_index >= static_cast<int>(filament_weights.size())) {
            return true; // Out-of-range → can't prove unused, be safe.
        }
        return filament_weights[tool_index] > 0.0;
    };

    auto& ams = AmsState::instance();

    if (ams.is_available()) {
        // AMS path: check ToolMapping.material_mismatch flags
        auto mappings = detail_view_->get_filament_mappings();
        auto tool_info = detail_view_->get_filament_tool_info();
        const auto& slots = detail_view_->get_available_slots();

        for (const auto& m : mappings) {
            if (!m.material_mismatch) {
                continue;
            }
            if (!tool_is_used(m.tool_index)) {
                spdlog::debug("[PrintStartController] Skipping T{} mismatch — "
                              "tool has zero filament usage in gcode",
                              m.tool_index);
                continue;
            }

            MaterialMismatchDetail detail;
            detail.tool_index = m.tool_index;

            // Get expected material from gcode tool info. Look up by real
            // tool_index — tool_info may be used-filtered (compacted), so its
            // vector position no longer equals the tool number.
            if (const auto* tool =
                    helix::ui::FilamentMappingCard::find_by_tool_index(tool_info, m.tool_index)) {
                detail.expected_material = tool->material;
            }

            // Get loaded material from the mapped AMS slot
            for (const auto& slot : slots) {
                if (slot.slot_index == m.mapped_slot && slot.backend_index == m.mapped_backend) {
                    detail.loaded_material = slot.material;
                    break;
                }
            }

            // Skip if either material is unknown (can't warn about unknowns)
            if (detail.expected_material.empty() || detail.loaded_material.empty()) {
                continue;
            }

            // Look up temperature ranges from the filament database
            auto expected_info = filament::find_material(detail.expected_material);
            if (expected_info) {
                detail.expected_nozzle_min = expected_info->nozzle_min;
                detail.expected_nozzle_max = expected_info->nozzle_max;
                detail.expected_bed_temp = expected_info->bed_temp;
            }

            auto loaded_info = filament::find_material(detail.loaded_material);
            if (loaded_info) {
                detail.loaded_nozzle_min = loaded_info->nozzle_min;
                detail.loaded_nozzle_max = loaded_info->nozzle_max;
                detail.loaded_bed_temp = loaded_info->bed_temp;
            }

            mismatches.push_back(std::move(detail));
        }
    } else {
        // Non-AMS path: compare gcode filament_type vs external spool
        const auto& gcode_materials = detail_view_->get_filament_materials();
        if (gcode_materials.empty()) {
            return mismatches;
        }

        auto spool_info = SettingsManager::instance().get_external_spool_info();
        if (!spool_info || spool_info->material.empty()) {
            return mismatches;
        }

        // Check the first tool (single extruder)
        const auto& expected = gcode_materials[0];
        if (expected.empty()) {
            return mismatches;
        }

        if (!helix::FilamentMapper::materials_match(expected, spool_info->material)) {
            MaterialMismatchDetail detail;
            detail.tool_index = 0;
            detail.expected_material = expected;
            detail.loaded_material = spool_info->material;

            // Temperature from filament database for expected material
            auto expected_info = filament::find_material(expected);
            if (expected_info) {
                detail.expected_nozzle_min = expected_info->nozzle_min;
                detail.expected_nozzle_max = expected_info->nozzle_max;
                detail.expected_bed_temp = expected_info->bed_temp;
            }

            // Temperature from external spool (user-set) or fall back to database
            if (spool_info->nozzle_temp_min > 0 && spool_info->nozzle_temp_max > 0) {
                detail.loaded_nozzle_min = spool_info->nozzle_temp_min;
                detail.loaded_nozzle_max = spool_info->nozzle_temp_max;
                detail.loaded_bed_temp = spool_info->bed_temp;
            } else {
                auto loaded_info = filament::find_material(spool_info->material);
                if (loaded_info) {
                    detail.loaded_nozzle_min = loaded_info->nozzle_min;
                    detail.loaded_nozzle_max = loaded_info->nozzle_max;
                    detail.loaded_bed_temp = loaded_info->bed_temp;
                }
            }

            mismatches.push_back(std::move(detail));
        }
    }

    if (!mismatches.empty()) {
        spdlog::info("[PrintStartController] {} material mismatch(es) detected", mismatches.size());
    }
    return mismatches;
}

void PrintStartController::show_material_mismatch_warning(
    const std::vector<MaterialMismatchDetail>& mismatches) {
    // Close any existing dialog first
    if (material_mismatch_modal_) {
        helix::ui::modal_hide(material_mismatch_modal_);
        material_mismatch_modal_ = nullptr;
    }

    std::string message;

    if (mismatches.size() == 1) {
        // Single-tool format: "This file was sliced for X but Y is loaded."
        const auto& m = mismatches[0];
        message = fmt::format(lv_tr("This file was sliced for {} but {} is loaded."),
                              m.expected_material, m.loaded_material);

        // Add temperature details if available
        if (m.expected_nozzle_min > 0 && m.loaded_nozzle_min > 0) {
            message += "\n\n";
            message +=
                fmt::format("  {} {}: {}\u2013{}°C {}, {}°C {}\n"
                            "  {} {}: {}\u2013{}°C {}, {}°C {}",
                            LV_SYMBOL_BULLET, m.expected_material, m.expected_nozzle_min,
                            m.expected_nozzle_max, lv_tr("nozzle"), m.expected_bed_temp,
                            lv_tr("bed"), LV_SYMBOL_BULLET, m.loaded_material, m.loaded_nozzle_min,
                            m.loaded_nozzle_max, lv_tr("nozzle"), m.loaded_bed_temp, lv_tr("bed"));
        }
    } else {
        // Multi-tool format: list each mismatched tool
        message = lv_tr("These tools have incompatible materials loaded:");
        message += "\n\n";
        for (const auto& m : mismatches) {
            std::string expected_temps;
            if (m.expected_nozzle_min > 0) {
                expected_temps =
                    fmt::format(" ({}\u2013{}°C)", m.expected_nozzle_min, m.expected_nozzle_max);
            }
            std::string loaded_temps;
            if (m.loaded_nozzle_min > 0) {
                loaded_temps =
                    fmt::format(" ({}\u2013{}°C)", m.loaded_nozzle_min, m.loaded_nozzle_max);
            }
            // "needs X (range): You have Y (range)" \u2014 clearer than the old
            // "X -> Y" form, which read as a transformation rather than a
            // comparison. Two short clauses joined by a colon scan well.
            message += fmt::format("  {} T{}: {} {}{}: {} {}{}\n", LV_SYMBOL_BULLET, m.tool_index,
                                   lv_tr("needs"), m.expected_material, expected_temps,
                                   lv_tr("you have"), m.loaded_material, loaded_temps);
        }
    }

    message += "\n\n";
    message += lv_tr("Printing with the wrong material can cause clogs, poor adhesion, "
                     "or failed prints.");

    // Static buffer for message — must persist during modal lifetime.
    static HELIX_PSRAM_BSS char message_buffer[2048];
    snprintf(message_buffer, sizeof(message_buffer), "%s", message.c_str());

    material_mismatch_modal_ = helix::ui::modal_show_confirmation(
        lv_tr("Material Mismatch"), message_buffer, ModalSeverity::Warning, lv_tr("Start Anyway"),
        on_material_mismatch_proceed_static, on_material_mismatch_cancel_static, this);

    if (!material_mismatch_modal_) {
        spdlog::error("[PrintStartController] Failed to create material mismatch warning dialog");
        if (update_print_button_) {
            update_print_button_();
        }
        return;
    }

    spdlog::debug("[PrintStartController] Material mismatch warning shown for {} tool(s)",
                  mismatches.size());
}

void PrintStartController::on_material_mismatch_proceed_static(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStartController] on_material_mismatch_proceed_static");
    auto* self = static_cast<PrintStartController*>(lv_event_get_user_data(e));
    if (self) {
        if (self->material_mismatch_modal_) {
            helix::ui::modal_hide(self->material_mismatch_modal_);
            self->material_mismatch_modal_ = nullptr;
        }
        self->execute_print_start();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStartController::on_material_mismatch_cancel_static(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStartController] on_material_mismatch_cancel_static");
    auto* self = static_cast<PrintStartController*>(lv_event_get_user_data(e));
    if (self) {
        if (self->material_mismatch_modal_) {
            helix::ui::modal_hide(self->material_mismatch_modal_);
            self->material_mismatch_modal_ = nullptr;
        }
        if (self->update_print_button_) {
            self->update_print_button_();
        }
        if (self->on_print_cancelled_) {
            self->on_print_cancelled_();
        }
        spdlog::debug("[PrintStartController] Print cancelled by user (material mismatch warning)");
    }
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// Filament Remap Lifecycle
// ============================================================================

bool PrintStartController::should_warn_remap_unsupported(
    const helix::printer::ToolMappingCapabilities& caps, bool applies_via_preprint) {
    // A backend that applies the remap via its firmware-native pre-print path
    // (Snapmaker U1) honors the user's choice through build_preprint_gcode()
    // even though caps.editable is false — so the toast would be a stale false
    // alarm. Only warn when the backend can neither edit its mapping nor apply
    // it via a pre-print send.
    if (applies_via_preprint) {
        return false;
    }
    return !caps.supported || !caps.editable;
}

bool PrintStartController::apply_filament_remaps() {
    if (!detail_view_) {
        return false;
    }

    auto mappings = detail_view_->get_filament_mappings();
    if (mappings.empty()) {
        return false;
    }

    // Check if any mapping is non-auto (user made explicit choices)
    bool has_explicit_mapping = false;
    for (const auto& m : mappings) {
        if (!m.is_auto && m.mapped_slot >= 0) {
            has_explicit_mapping = true;
            break;
        }
    }

    if (!has_explicit_mapping) {
        spdlog::debug("[PrintStartController] All mappings are auto — no remaps needed");
        return false;
    }

    // Get the AMS backend for remapping
    auto& ams = AmsState::instance();
    if (!ams.is_available()) {
        spdlog::warn("[PrintStartController] AMS not available for remapping");
        return false;
    }

    // Use the first explicit mapping's backend.
    // Multi-backend remap is not yet supported — warn if mappings span backends.
    int backend_idx = 0;
    bool backend_set = false;
    for (const auto& m : mappings) {
        if (m.mapped_backend >= 0) {
            if (!backend_set) {
                backend_idx = m.mapped_backend;
                backend_set = true;
            } else if (m.mapped_backend != backend_idx) {
                spdlog::warn("[PrintStartController] Mappings span multiple backends "
                             "({} and {}) — only backend {} will be remapped",
                             backend_idx, m.mapped_backend, backend_idx);
                break;
            }
        }
    }

    auto* backend = ams.get_backend(backend_idx);
    if (!backend) {
        spdlog::warn("[PrintStartController] Backend {} not found", backend_idx);
        return false;
    }

    // Backends that apply the remap via their firmware-native pre-print path
    // (Snapmaker U1: requires_preprint_send → build_preprint_gcode emits
    // SET_PRINT_EXTRUDER_MAP / SET_PRINT_USED_EXTRUDERS) honor the user's choice
    // even though their tool-mapping capabilities report editable=false. For
    // those, skip the generic set_tool_mapping() path SILENTLY — the pre-print
    // send (fired later from execute_print_start) does the work. Only warn when
    // the backend can NEITHER edit its mapping NOR apply it via a pre-print send.
    auto caps = backend->get_tool_mapping_capabilities();
    if (!caps.editable) {
        if (should_warn_remap_unsupported(caps, backend->requires_preprint_send())) {
            spdlog::warn("[PrintStartController] Backend (idx={}) does not support editable tool "
                         "mapping — {} explicit remap(s) will be ignored",
                         backend_idx, mappings.size());
            NOTIFY_WARNING("Filament remap not supported on this printer — print will use "
                           "the firmware's current tool mapping");
        } else {
            spdlog::debug("[PrintStartController] Backend (idx={}) applies remap via its "
                          "pre-print path — skipping generic remap send (no warning)",
                          backend_idx);
        }
        return false;
    }

    // Snapshot current firmware mapping BEFORE sending any remaps
    saved_tool_mapping_ = backend->get_tool_mapping();
    saved_backend_index_ = backend_idx;

    spdlog::info("[PrintStartController] Saved firmware mapping ({} tools) from backend {}",
                 saved_tool_mapping_.size(), backend_idx);

    // Send remap commands for changed mappings only.
    // Note: set_tool_mapping() is fire-and-forget (async G-code via WebSocket).
    // There is a small race window where the print could start before Klipper
    // processes all remap commands. In practice the window is negligible because
    // prep_manager->start_print() also sends an async request to Moonraker.
    int remaps_sent = 0;
    for (const auto& m : mappings) {
        if (m.is_auto || m.mapped_slot < 0) {
            continue; // Skip auto mappings — firmware decides
        }

        // Check if this mapping differs from current firmware mapping
        int current_slot = -1;
        if (m.tool_index >= 0 && m.tool_index < static_cast<int>(saved_tool_mapping_.size())) {
            current_slot = saved_tool_mapping_[m.tool_index];
        }

        if (m.mapped_slot == current_slot) {
            spdlog::debug("[PrintStartController] T{} already mapped to slot {} — skipping",
                          m.tool_index, m.mapped_slot);
            continue;
        }

        spdlog::info("[PrintStartController] Remapping T{}: slot {} -> slot {}", m.tool_index,
                     current_slot, m.mapped_slot);

        auto err = backend->set_tool_mapping(m.tool_index, m.mapped_slot);
        if (err.result != AmsResult::SUCCESS) {
            spdlog::error("[PrintStartController] Failed to remap T{}: {}", m.tool_index,
                          err.technical_msg);
            // Continue trying other remaps — partial success is better than none
        } else {
            ++remaps_sent;
        }
    }

    if (remaps_sent > 0) {
        spdlog::info("[PrintStartController] Sent {} remap command(s)", remaps_sent);
        persist_remap_state();
        return true;
    }

    // No actual remaps sent (all matched current mapping)
    saved_tool_mapping_.clear();
    saved_backend_index_ = -1;
    return false;
}

void PrintStartController::observe_print_state_for_restore() {
    auto* subject = printer_state_.get_print_state_enum_subject();
    if (!subject) {
        spdlog::warn("[PrintStartController] No print state subject — cannot auto-restore mapping");
        return;
    }

    // Note: observe_int_sync fires immediately with the current value.
    // At registration time, state is typically STANDBY (print hasn't started yet).
    // We only trigger restore on terminal states (COMPLETE/CANCELLED/ERROR),
    // NOT STANDBY — otherwise the immediate fire would undo our remaps.
    print_state_observer_ = observe_int_sync<PrintStartController>(
        subject, this, [](PrintStartController* self, int state_val) {
            auto state = static_cast<PrintJobState>(state_val);
            if (state == PrintJobState::COMPLETE || state == PrintJobState::CANCELLED ||
                state == PrintJobState::ERROR) {
                self->restore_filament_mapping();
                self->print_state_observer_.reset();
            }
        });

    spdlog::debug("[PrintStartController] Observing print state for mapping restore");
}

void PrintStartController::observe_klippy_state_for_restore() {
    if (klippy_state_observer_) {
        return; // Already waiting — a second deferral must not stack observers
    }

    auto* subject = printer_state_.get_klippy_state_subject();
    if (!subject) {
        spdlog::warn("[PrintStartController] No klippy state subject — deferred restore cannot "
                     "self-resolve; pending_remap.json will replay on next startup");
        return;
    }

    // Fires immediately with the current value, which is by definition not READY
    // here (we only get called from the not-ready branch), so the first fire is a
    // no-op. Without this the snapshot would sit until a full app restart, which
    // is the only other thing that replays pending_remap.json.
    klippy_state_observer_ = observe_int_sync<PrintStartController>(
        subject, this, [](PrintStartController* self, int state_val) {
            if (static_cast<KlippyState>(state_val) != KlippyState::READY) {
                return;
            }
            spdlog::info(
                "[PrintStartController] Klipper ready — retrying deferred mapping restore");
            self->klippy_state_observer_.reset();
            self->restore_filament_mapping();
        });

    spdlog::debug("[PrintStartController] Observing klippy state for deferred mapping restore");
}

void PrintStartController::restore_filament_mapping() {
    if (saved_tool_mapping_.empty() || saved_backend_index_ < 0) {
        return; // Nothing to restore
    }

    // A restore sent to a halted Klipper is refused, and set_tool_mapping()
    // cannot tell us: every native backend routes into
    // AmsSubscriptionBackend::execute_gcode(), which returns success
    // unconditionally and reports refusals only through an async log callback.
    // So the readiness check has to happen HERE, before we treat the snapshot as
    // spent. Getting this wrong stranded the printer on the print's mapping with
    // the recovery record deleted (#1270), and a halted Klipper at print end is
    // the normal shape of a cancelled or errored print — precisely when restore
    // runs.
    const auto klippy =
        static_cast<KlippyState>(lv_subject_get_int(printer_state_.get_klippy_state_subject()));
    if (klippy != KlippyState::READY) {
        spdlog::info("[PrintStartController] Klipper not ready (state={}) — deferring restore of "
                     "{} mapping(s); snapshot and pending_remap.json retained",
                     static_cast<int>(klippy), saved_tool_mapping_.size());
        observe_klippy_state_for_restore();
        return; // NOT delivered: keep saved_tool_mapping_ and the persisted file
    }

    auto& ams = AmsState::instance();
    auto* backend = ams.get_backend(saved_backend_index_);
    if (!backend) {
        spdlog::warn("[PrintStartController] Backend {} gone — cannot restore mapping",
                     saved_backend_index_);
        saved_tool_mapping_.clear();
        saved_backend_index_ = -1;
        return;
    }

    // Get current mapping to compare
    auto current_mapping = backend->get_tool_mapping();

    int restores_sent = 0;
    for (size_t i = 0; i < saved_tool_mapping_.size(); ++i) {
        int saved_slot = saved_tool_mapping_[i];
        int current_slot = (i < current_mapping.size()) ? current_mapping[i] : -1;

        if (saved_slot != current_slot) {
            spdlog::info("[PrintStartController] Restoring T{}: slot {} -> slot {}", i,
                         current_slot, saved_slot);
            auto err = backend->set_tool_mapping(static_cast<int>(i), saved_slot);
            if (err.result != AmsResult::SUCCESS) {
                spdlog::error("[PrintStartController] Failed to restore T{}: {}", i,
                              err.technical_msg);
            } else {
                ++restores_sent;
            }
        }
    }

    spdlog::info("[PrintStartController] Sent {} restore command(s) on print end", restores_sent);

    // Backends that echo their mapping back let us wait for firmware truth
    // instead of trusting a send. Those that don't fall back to "sent to a ready
    // Klipper counts as delivered" — weaker, but waiting for a confirmation that
    // can never arrive would strand the record forever.
    if (!backend->reports_firmware_tool_mapping()) {
        spdlog::debug("[PrintStartController] Backend does not echo its tool mapping — treating "
                      "the send as delivery");
        finish_restore();
        return;
    }

    // Nothing was sent AND the mapping already matches: already correct, no
    // firmware round trip is coming to confirm. Done.
    if (restores_sent == 0) {
        finish_restore();
        return;
    }

    restore_generation_at_send_ = backend->firmware_tool_mapping_generation();
    awaiting_restore_confirmation_ = true;
    spdlog::info("[PrintStartController] Awaiting firmware confirmation of restore (generation {})",
                 restore_generation_at_send_);
    observe_ams_data_for_confirmation();
}

void PrintStartController::finish_restore() {
    awaiting_restore_confirmation_ = false;
    ams_data_observer_.reset();
    saved_tool_mapping_.clear();
    saved_backend_index_ = -1;
    clear_persisted_remap_state();
}

void PrintStartController::observe_ams_data_for_confirmation() {
    if (ams_data_observer_) {
        return;
    }

    auto* subject = AmsState::instance().get_ams_data_revision_subject();
    if (!subject) {
        return;
    }

    ams_data_observer_ = observe_int_sync<PrintStartController>(
        subject, this, [](PrintStartController* self, int) { self->check_restore_confirmed(); });
}

void PrintStartController::check_restore_confirmed() {
    if (!awaiting_restore_confirmation_ || saved_tool_mapping_.empty() ||
        saved_backend_index_ < 0) {
        return;
    }

    auto* backend = AmsState::instance().get_backend(saved_backend_index_);
    if (!backend) {
        // Backend disappeared mid-wait. Nothing can confirm now, and holding the
        // record would block every future restore, so give up on THIS one but
        // leave the persisted file for the next startup to replay.
        spdlog::warn("[PrintStartController] Backend {} gone while awaiting restore confirmation",
                     saved_backend_index_);
        awaiting_restore_confirmation_ = false;
        ams_data_observer_.reset();
        return;
    }

    // The revision subject is a coarse wake-up — it fires for our own optimistic
    // writes too. The generation is what separates "the printer said so" from
    // "we said so", so an unmoved generation means this tick proves nothing.
    const uint64_t generation = backend->firmware_tool_mapping_generation();
    if (generation == restore_generation_at_send_) {
        return;
    }

    auto current = backend->get_tool_mapping();
    for (size_t i = 0; i < saved_tool_mapping_.size(); ++i) {
        const int want = saved_tool_mapping_[i];
        const int have = (i < current.size()) ? current[i] : -1;
        if (want != have) {
            // Firmware reported something, but not what we asked for. Keep
            // waiting: a multi-lane restore lands one delta at a time, so a
            // partial match is the normal intermediate state, not a failure.
            spdlog::debug("[PrintStartController] Restore not yet confirmed: T{} reads slot {}, "
                          "want {} (generation {})",
                          i, have, want, generation);
            return;
        }
    }

    spdlog::info("[PrintStartController] Firmware confirmed restore of {} mapping(s) (generation "
                 "{} -> {})",
                 saved_tool_mapping_.size(), restore_generation_at_send_, generation);
    finish_restore();
}

// ============================================================================
// Crash Recovery Persistence
// ============================================================================

static constexpr const char* PENDING_REMAP_FILENAME = "pending_remap.json";

static std::filesystem::path pending_remap_path() {
    return std::filesystem::path(helix::get_user_config_dir()) / PENDING_REMAP_FILENAME;
}

void PrintStartController::persist_remap_state() {
    namespace fs = std::filesystem;

    if (saved_tool_mapping_.empty() || saved_backend_index_ < 0) {
        return;
    }

    nlohmann::json j;
    j["backend_index"] = saved_backend_index_;
    j["tool_mapping"] = saved_tool_mapping_;

    auto path = pending_remap_path();
    try {
        fs::create_directories(path.parent_path());
        std::ofstream ofs(path);
        if (ofs.is_open()) {
            ofs << j.dump(2);
            spdlog::debug("[PrintStartController] Persisted remap state to {}", path.string());
        }
    } catch (const std::exception& e) {
        spdlog::warn("[PrintStartController] Failed to persist remap state: {}", e.what());
    }
}

void PrintStartController::clear_persisted_remap_state() {
    namespace fs = std::filesystem;

    auto path = pending_remap_path();
    try {
        if (fs::exists(path)) {
            fs::remove(path);
            spdlog::debug("[PrintStartController] Cleared persisted remap state");
        }
    } catch (const std::exception& e) {
        spdlog::warn("[PrintStartController] Failed to clear remap state: {}", e.what());
    }
}

void PrintStartController::recover_pending_remap() {
    namespace fs = std::filesystem;

    auto path = pending_remap_path();
    if (!fs::exists(path)) {
        return;
    }

    try {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            return;
        }

        auto j = nlohmann::json::parse(ifs);
        int backend_idx = j.value("backend_index", -1);
        auto mapping = j.value("tool_mapping", std::vector<int>{});

        if (backend_idx < 0 || mapping.empty()) {
            spdlog::debug("[PrintStartController] Invalid pending remap file — removing");
            clear_persisted_remap_state();
            return;
        }

        // Load saved state and decide between immediate vs deferred restore.
        saved_tool_mapping_ = std::move(mapping);
        saved_backend_index_ = backend_idx;

        // If a print is still active, the user's remap is still LOAD-BEARING —
        // reverting now would silently swap filament routing under a running
        // print. Defer the restore until the print reaches a terminal state.
        // observe_print_state_for_restore registers an observer that fires
        // restore_filament_mapping on COMPLETE/CANCELLED/ERROR; the immediate-
        // fire on the current PRINTING/PAUSED value is a no-op there.
        auto current_state = static_cast<PrintJobState>(
            lv_subject_get_int(printer_state_.get_print_state_enum_subject()));
        bool print_active =
            (current_state == PrintJobState::PRINTING || current_state == PrintJobState::PAUSED);

        if (print_active) {
            spdlog::info("[PrintStartController] Crash recovery: found pending remap "
                         "({} tools, backend {}) — print still active (state={}), "
                         "deferring restore until print ends",
                         saved_tool_mapping_.size(), saved_backend_index_,
                         static_cast<int>(current_state));
            observe_print_state_for_restore();
        } else {
            spdlog::info("[PrintStartController] Crash recovery: found pending remap "
                         "({} tools, backend {}) — restoring",
                         saved_tool_mapping_.size(), saved_backend_index_);
            restore_filament_mapping();
        }
    } catch (const std::exception& e) {
        spdlog::warn("[PrintStartController] Failed to load pending remap: {}", e.what());
        clear_persisted_remap_state();
    }
}

} // namespace helix::ui
