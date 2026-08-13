// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"

#include "ams_types.h"
#include "async_lifetime_guard.h"
#include "filament_mapper.h"

#include <functional>
#include <lvgl.h>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Forward declarations
namespace helix {
class PrinterState;
}
class IMoonrakerAPI;
struct PrintFileData;
class PrintStartControllerTestAccess; // test-only friend (global scope)

namespace helix::ui {

class PrintSelectDetailView;

/**
 * @brief Controller for print initiation workflow
 *
 * Handles the print start process including:
 * - Filament availability warnings (runout sensor)
 * - AMS color matching validation
 * - Actual print start via PrintPreparationManager
 *
 * This controller does NOT own the file selection state or the detail view.
 * It receives file information via set_file() and delegates the actual
 * print start to PrintPreparationManager (owned by the detail view).
 *
 * @pattern Extracted controller pattern - separates print initiation workflow
 *          from the larger PrintSelectPanel.
 */
class PrintStartController {
  public:
    using PrintStartedCallback = std::function<void()>;
    using PrintCancelledCallback = std::function<void()>;
    using UpdatePrintButtonCallback = std::function<void()>;
    using HideDetailViewCallback = std::function<void()>;
    using ShowDetailViewCallback = std::function<void()>;
    using NavigateToPrintStatusCallback = std::function<void()>;

    /**
     * @brief Construct controller with required dependencies
     *
     * @param printer_state Reference to PrinterState for capability queries
     * @param api Pointer to IMoonrakerAPI (may be nullptr initially)
     */
    PrintStartController(PrinterState& printer_state, IMoonrakerAPI* api);
    ~PrintStartController();

    // Non-copyable
    PrintStartController(const PrintStartController&) = delete;
    PrintStartController& operator=(const PrintStartController&) = delete;

    /**
     * @brief Set the API (can be null initially, set later)
     */
    void set_api(IMoonrakerAPI* api);

    /**
     * @brief Set the detail view for prep manager access
     *
     * The detail view owns the PrintPreparationManager which is needed
     * for the actual print start sequence.
     */
    void set_detail_view(PrintSelectDetailView* detail_view);

    /**
     * @brief Set the file to print
     *
     * @param filename Raw filename (e.g., "benchy.gcode")
     * @param path Directory path relative to gcodes root
     * @param filament_colors Hex colors per tool for AMS matching
     * @param thumbnail_path Extracted thumbnail path (for USB/embedded thumbnails)
     */
    void set_file(const std::string& filename, const std::string& path,
                  const std::vector<std::string>& filament_colors,
                  const std::string& thumbnail_path = "");

    /**
     * @brief Initiate print workflow
     *
     * Entry point for starting a print. Performs checks:
     * 1. Printer state validation (not already printing)
     * 2. Filament runout sensor check (warns if no filament)
     * 3. AMS color match check (warns on mismatches)
     *
     * If all checks pass (or user confirms warnings), executes the print.
     */
    void initiate();

    /**
     * @brief Reprint a file already on the printer.
     *
     * Shares the U1 native pre-print send with the normal start path, but uses
     * the lightweight job().start_print (no upload/prep — the file is already on
     * the printer). @p tools_used drives the SET_PRINT_USED_EXTRUDERS
     * computation (empty → no native send, just starts). @p on_started /
     * @p on_error are invoked on the MAIN thread.
     *
     * @param filename   Raw filename to reprint (already on the printer).
     * @param path       Directory path relative to gcodes root (unused by the
     *                   lightweight start; kept for symmetry with set_file()).
     * @param tools_used Set of tool indices used by the file (U1 native send).
     * @param on_started Called on success (main thread).
     * @param on_error   Called on failure (main thread).
     */
    void initiate_reprint(const std::string& filename, const std::string& path,
                          const std::set<int>& tools_used, std::function<void()> on_started,
                          std::function<void()> on_error);

    /**
     * @brief Check if controller is ready to start a print
     *
     * @return true if filename is set and detail view is available
     */
    [[nodiscard]] bool is_ready() const;

    // === Callbacks ===

    void set_on_print_started(PrintStartedCallback cb) {
        on_print_started_ = std::move(cb);
    }
    void set_on_print_cancelled(PrintCancelledCallback cb) {
        on_print_cancelled_ = std::move(cb);
    }
    void set_update_print_button(UpdatePrintButtonCallback cb) {
        update_print_button_ = std::move(cb);
    }
    void set_hide_detail_view(HideDetailViewCallback cb) {
        hide_detail_view_ = std::move(cb);
    }
    void set_show_detail_view(ShowDetailViewCallback cb) {
        show_detail_view_ = std::move(cb);
    }
    void set_navigate_to_print_status(NavigateToPrintStatusCallback cb) {
        navigate_to_print_status_ = std::move(cb);
    }

    /**
     * @brief Set the subject that controls print button enabled state
     *
     * The controller sets this to 0 when print is initiated and relies
     * on update_print_button_ callback for re-enabling on cancel/failure.
     */
    void set_can_print_subject(lv_subject_t* subject) {
        can_print_subject_ = subject;
    }

  private:
    // Test-only access to the private pre-print filament gate (issue 1: shared
    // auto-unload suppression on both initiate() and the insufficient path).
    friend class ::PrintStartControllerTestAccess;

    /**
     * @brief Execute the actual print start
     *
     * Called directly when no warning needed, or after user confirms warning dialog.
     * Delegates to PrintPreparationManager for file operations and Moonraker API calls.
     */
    void execute_print_start();

    /**
     * @brief Send the Snapmaker U1 firmware-native print_task_config gcode, then continue.
     *
     * When the active AMS backend's RemapStrategy is SnapmakerNative, the U1
     * firmware requires SET_PRINT_USED_EXTRUDERS / SET_PRINT_EXTRUDER_MAP to be
     * emitted BEFORE PRINT_START (they error mid-print). Builds that gcode from
     * @p tools_used and @p remap (supplied by the caller), sends it, and only invokes
     * @p on_done (which starts the print) after the send succeeds. On send error it
     * surfaces a user-visible failure, calls @p on_abort, and does NOT start the print.
     * If there is nothing to send (empty tools or empty gcode), @p on_done runs immediately.
     *
     * Callbacks land on the WebSocket background thread; the body is deferred to
     * the main thread via the lifetime token.
     *
     * @param tools_used  Set of tool indices used by the file (from detail view or reprint).
     * @param remap       Tool-to-slot remap map (from detail view or reprint).
     * @param on_done     Called on success — proceeds with the actual print start.
     * @param on_abort    Called on error — caller is responsible for re-enabling UI state.
     */
    void send_snapmaker_preprint_then(const std::set<int>& tools_used,
                                      const std::map<int, int>& remap,
                                      std::function<void()> on_done,
                                      std::function<void()> on_abort);

    /**
     * @brief Show filament warning dialog
     *
     * Called when the pre-print filament check finds no filament. User can
     * proceed or cancel. @p message overrides the default body text — used to
     * name the specific tool(s)/lane(s) that are empty (AMS lane-truth path).
     * Empty -> the generic "runout sensor indicates no filament" text (non-AMS).
     */
    void show_filament_warning(const std::string& message = "");

    /**
     * @brief Required tools whose AMS lane is genuinely empty (lane truth).
     *
     * Scopes the pre-print filament check to the tools the print actually uses
     * (detail_view_->get_tools_used()) and the effective tool→slot remap
     * (detail_view_->get_effective_remap()), consulting the AMS backend's
     * authoritative per-slot presence via
     * FilamentSensorManager::find_empty_required_lanes(). Returns {} when no AMS
     * backend manages filament (caller falls back to the aggregate sensor
     * check). @return (tool_index, slot_index) pairs.
     */
    std::vector<std::pair<int, int>> find_empty_required_lanes();

    /// Build the named-tool/lane warning body for show_filament_warning().
    std::string build_empty_lane_message(const std::vector<std::pair<int, int>>& empty) const;

    /**
     * @brief Shared pre-print filament-present gate (issue 1).
     *
     * Single source of truth for both initiate() and the insufficient-filament
     * Proceed continuation, so they behave identically:
     *   1. Backends that auto-unload after a print (AD5X IFS) leave the extruder
     *      empty by design → suppress the warning entirely.
     *   2. When an AMS backend manages filament, scope the check to the print's
     *      tools using lane truth (find_empty_required_lanes) and warn naming the
     *      offending tool/lane.
     *   3. Otherwise (non-AMS) fall back to the aggregate runout-sensor check.
     *
     * @return true if a warning dialog was shown (caller must return — the
     *         dialog drives continuation); false to proceed to the next check.
     */
    bool check_required_filament_present();

    /// Check FilamentMapper results for unresolved tools (replaces raw color comparison)
    std::vector<int> find_unresolved_tools();

    /// Show improved mismatch warning with color names and slot context
    void show_color_mismatch_warning(const std::vector<int>& unresolved_tools,
                                     const std::vector<helix::GcodeToolInfo>& tool_info);

    /// Per-tool material mismatch detail for the warning dialog
    struct MaterialMismatchDetail {
        int tool_index;
        std::string expected_material;
        std::string loaded_material;
        int expected_nozzle_min = 0;
        int expected_nozzle_max = 0;
        int expected_bed_temp = 0;
        int loaded_nozzle_min = 0;
        int loaded_nozzle_max = 0;
        int loaded_bed_temp = 0;
    };

    /// Find material mismatches for both AMS and non-AMS printers
    std::vector<MaterialMismatchDetail> find_material_mismatches();

    /// Show detailed material mismatch warning with temperature info
    void show_material_mismatch_warning(const std::vector<MaterialMismatchDetail>& mismatches);

    /// Continue the initiate() flow after the unresolved-tools check passes
    void continue_after_unresolved_check();

    /// Show warning when the assigned external spool doesn't have enough
    /// filament for the predicted print weight.
    void show_insufficient_filament_warning(float needed_g, float remaining_g);

    // Static callbacks for LVGL modal
    static void on_filament_warning_proceed_static(lv_event_t* e);
    static void on_filament_warning_cancel_static(lv_event_t* e);
    static void on_color_mismatch_proceed_static(lv_event_t* e);
    static void on_color_mismatch_cancel_static(lv_event_t* e);
    static void on_material_mismatch_proceed_static(lv_event_t* e);
    static void on_material_mismatch_cancel_static(lv_event_t* e);
    static void on_insufficient_filament_proceed_static(lv_event_t* e);
    static void on_insufficient_filament_cancel_static(lv_event_t* e);

    // === Dependencies ===
    PrinterState& printer_state_;
    IMoonrakerAPI* api_ = nullptr;
    PrintSelectDetailView* detail_view_ = nullptr;
    lv_subject_t* can_print_subject_ = nullptr;

    // Guards background-thread API callbacks (Snapmaker U1 pre-print send) so a
    // dismissed controller doesn't UAF. From bg threads use lifetime_.token()
    // then tok.defer(...). See async_lifetime_guard.h.
    helix::AsyncLifetimeGuard lifetime_;

    // === File State ===
    std::string filename_;
    std::string path_;
    std::vector<std::string> filament_colors_;
    std::string thumbnail_path_; ///< Pre-extracted thumbnail for USB/embedded files

    // === Modal References ===
    lv_obj_t* filament_warning_modal_ = nullptr;
    lv_obj_t* color_mismatch_modal_ = nullptr;
    lv_obj_t* material_mismatch_modal_ = nullptr;
    lv_obj_t* insufficient_filament_modal_ = nullptr;

    // === Callbacks ===
    PrintStartedCallback on_print_started_;
    PrintCancelledCallback on_print_cancelled_;
    UpdatePrintButtonCallback update_print_button_;
    HideDetailViewCallback hide_detail_view_;
    ShowDetailViewCallback show_detail_view_;
    NavigateToPrintStatusCallback navigate_to_print_status_;

    // === Filament Remap State ===
    // Saved firmware mapping before we send remaps (for restore on print end)
    std::vector<int> saved_tool_mapping_;
    int saved_backend_index_ = -1; ///< Which backend we remapped

    // Observer for print state changes (to restore mapping on print end)
    ObserverGuard print_state_observer_;

    // === Filament Remap Methods ===
    /// Snapshot current firmware mapping, send remap commands, return true if remaps were sent
    bool apply_filament_remaps();

    /**
     * @brief Whether to warn the user that their explicit remap can't be honored.
     *
     * Pure decision helper for apply_filament_remaps(). A backend that applies
     * the remap via its firmware-native pre-print path (requires_preprint_send,
     * e.g. Snapmaker U1) honors the user's choice through build_preprint_gcode()
     * even though its tool-mapping capabilities report editable=false — so the
     * "remap not supported" toast is a STALE false alarm and must be suppressed.
     * Only warn when the backend can NEITHER edit its mapping NOR apply it via a
     * pre-print send (a backend that genuinely cannot honor the remap at all).
     *
     * @param caps              backend->get_tool_mapping_capabilities()
     * @param applies_via_preprint backend->requires_preprint_send()
     * @return true if the unsupported-remap warning toast should be shown
     */
    [[nodiscard]] static bool
    should_warn_remap_unsupported(const helix::printer::ToolMappingCapabilities& caps,
                                  bool applies_via_preprint);

    /// Restore original firmware mapping (called on print end)
    void restore_filament_mapping();

    /// Set up observer for print state to auto-restore mapping
    void observe_print_state_for_restore();

    // === Crash Recovery Persistence ===
    /// Save remap state to disk so it survives app restart
    void persist_remap_state();

    /// Delete persisted remap state (called after successful restore)
    void clear_persisted_remap_state();

  public:
    /// Check for and restore any pending remap from a previous crashed session.
    /// Call once after AMS backends are initialized.
    void recover_pending_remap();
};

} // namespace helix::ui
