// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_print_select_detail_view.h"

#include "ui_callback_helpers.h"
#include "ui_error_reporting.h"
#include "ui_filename_utils.h"
#include "ui_gcode_viewer.h"
#include "ui_icon.h"
#include "ui_modal.h"
#include "ui_nav_manager.h"
#include "ui_print_preparation_manager.h"
#include "ui_toast_manager.h"
#include "ui_update_queue.h"
#include "ui_utils.h"

#include "ams_state.h"
#include "app_globals.h"
#include "color_utils.h"
#include "config.h"
#include "display_settings_manager.h"
#include "gcode_parser.h"
#include "i_moonraker_api.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "memory_utils.h"
#include "observer_factory.h"
#include "runtime_config.h"
#include "settings_manager.h"
#include "theme_manager.h"
#include "tool_state.h"

#include <spdlog/spdlog.h>

#include <fstream>

namespace helix::ui {

// ============================================================================
// Static instance pointer for callback access
// ============================================================================

// Static instance pointer for the helper functions in this TU (currently
// just update_prep_time_label) to reach the live detail view. Only one
// detail view exists at a time; set during init_subjects() / cleared in the
// destructor.
static PrintSelectDetailView* s_detail_view_instance = nullptr;

// ============================================================================
// Static callback declarations
// ============================================================================

// Forward decl for the prep-time estimate refresh that runs after every
// toggle. Defined later in this TU.
static void update_prep_time_label();

// ============================================================================
// Lifecycle
// ============================================================================

PrintSelectDetailView::~PrintSelectDetailView() {
    // Clear static instance pointer
    if (s_detail_view_instance == this) {
        s_detail_view_instance = nullptr;
    }

    // lifetime_ destructor auto-invalidates all outstanding tokens

    // Clean up temp gcode file
    if (!temp_gcode_path_.empty()) {
        std::remove(temp_gcode_path_.c_str());
        temp_gcode_path_.clear();
    }

    // CRITICAL: During static destruction (app exit), LVGL may already be gone.
    // We check if LVGL is still initialized before calling any LVGL functions.
    if (!lv_is_initialized()) {
        spdlog::trace("[DetailView] Destroyed (LVGL already deinit)");
        return;
    }

    spdlog::trace("[DetailView] Destroyed");

    // Cancel the pre-flight readiness safety timer if still armed (LVGL is known
    // initialized here — checked above).
    if (preflight_ready_timeout_timer_) {
        lv_timer_delete(preflight_ready_timeout_timer_);
        preflight_ready_timeout_timer_ = nullptr;
    }

    // Unregister from NavigationManager (fallback if cleanup() wasn't called)
    if (overlay_root_) {
        NavigationManager::instance().unregister_overlay_instance(overlay_root_);
    }

    // Deinitialize subjects to disconnect observers before widgets are deleted
    // This prevents dangling pointers and frees observer linked lists
    if (subjects_initialized_) {
        subjects_.deinit_all();
        subjects_initialized_ = false;
    }

    // Clean up confirmation dialog if open
    if (confirmation_dialog_widget_) {
        helix::ui::modal_hide(confirmation_dialog_widget_);
        confirmation_dialog_widget_ = nullptr;
    }

    // Destructors can run from inside a queue_update() batch (unique_ptr
    // reassignment, owner reset()); a sync lv_obj_delete() there corrupts
    // LVGL's event list if another sync delete lands in the same batch
    // (#776, #190, #80). Deferred delete is safe outside a batch too.
    helix::ui::safe_delete_deferred(overlay_root_);
}

// ============================================================================
// Setup
// ============================================================================

void PrintSelectDetailView::init_subjects() {
    if (subjects_initialized_) {
        spdlog::debug("[DetailView] Subjects already initialized, skipping");
        return;
    }

    // Set static instance pointer for callbacks (must be before callback registration)
    s_detail_view_instance = this;

    // Per-option toggle callbacks are wired imperatively in
    // populate_option_rows() once the dynamic rows are created. The previous
    // hardcoded XML <event_cb callback="on_preprint_*_toggled"/> bindings are
    // gone — there is nothing to register here.

    // G-code viewer visibility mode (0=thumbnail, 1=3D, 2=2D)
    UI_MANAGED_SUBJECT_INT(detail_gcode_viewer_mode_, 0, "detail_gcode_viewer_mode", subjects_);
    // G-code loading indicator (0=hidden, 1=visible)
    UI_MANAGED_SUBJECT_INT(detail_gcode_loading_, 0, "detail_gcode_loading", subjects_);
    // Whether the viewer has rendered its first frame (0=no, 1=yes). The
    // thumbnail stays on top until this flips to hide the gray gap.
    UI_MANAGED_SUBJECT_INT(detail_viewer_first_frame_, 0, "detail_viewer_first_frame", subjects_);

    // Preview color mode: 0 = actual (loaded slot colors), 1 = sliced (slicer intent)
    UI_MANAGED_SUBJECT_INT(detail_prefer_sliced_colors_, 0, "detail_prefer_sliced_colors",
                           subjects_);

    // Filament mismatch warning (0=hidden, 1=visible)
    UI_MANAGED_SUBJECT_INT(filament_mismatch_, 0, "filament_mismatch", subjects_);

    // Filament mapping card visibility (0=hidden, 1=visible). Driven by
    // FilamentMappingCard::should_show() after each update(); XML binds
    // via bind_flag_if_eq in print_file_detail.xml.
    UI_MANAGED_SUBJECT_INT(filament_mapping_visible_, 0, "filament_mapping_visible", subjects_);

    // Legacy color swatches card visibility (0=hidden, 1=visible). Shown
    // only when the mapping card is NOT visible AND the file is multi-tool.
    // The two subjects are mutually exclusive by construction — see show()
    // and the metadata-derived-colors path.
    UI_MANAGED_SUBJECT_INT(color_swatches_visible_, 0, "color_swatches_visible", subjects_);

    // Empty-tools warning visibility (0=hidden, 1=visible). Set by
    // update_color_swatches() when any T-command-referenced slot is empty.
    UI_MANAGED_SUBJECT_INT(empty_tools_warning_, 0, "empty_tools_warning", subjects_);

    // Pre-print time estimate (formatted string for bind_text)
    UI_MANAGED_SUBJECT_STRING(prep_time_estimate_subject_, prep_time_estimate_buf_, "",
                              "preprint_estimate_text", subjects_);

    // Re-color the preview live when a slot's loaded color/presence changes
    // (filament reloaded). Static singleton subject -> plain ObserverGuard, no
    // lifetime token. Handler no-ops while the view is closed.
    slots_version_observer_ = observe_int_sync<PrintSelectDetailView>(
        AmsState::instance().get_slots_version_subject(), this,
        [](PrintSelectDetailView* self, int /*version*/) { self->on_ams_state_changed(); },
        AmsState::instance().get_subjects_lifetime());

    subjects_initialized_ = true;
    spdlog::debug("[DetailView] Initialized pre-print option subjects");
}

lv_obj_t* PrintSelectDetailView::create(lv_obj_t* parent_screen) {
    if (!parent_screen) {
        spdlog::error("[DetailView] Cannot create: parent_screen is null");
        return nullptr;
    }

    if (overlay_root_) {
        spdlog::warn("[DetailView] Detail view already exists");
        return overlay_root_;
    }

    parent_screen_ = parent_screen;

    overlay_root_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "print_file_detail", nullptr));

    if (!overlay_root_) {
        LOG_ERROR_INTERNAL("[DetailView] Failed to create detail view from XML");
        NOTIFY_ERROR(lv_tr("Failed to load file details"));
        return nullptr;
    }

    // Set responsive padding for content area
    lv_obj_t* content_container = lv_obj_find_by_name(overlay_root_, "content_container");
    if (content_container) {
        lv_coord_t padding = ui_get_header_content_padding();
        lv_obj_set_style_pad_all(content_container, padding, 0);
    }

    lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);

    // Store reference to print button for enable/disable state management
    print_button_ = lv_obj_find_by_name(overlay_root_, "print_button");

    // Find and configure G-code viewer widget
    gcode_viewer_ = lv_obj_find_by_name(overlay_root_, "detail_gcode_viewer");
    if (gcode_viewer_) {
        spdlog::debug("[DetailView] G-code viewer widget found");
        ui_gcode_viewer_disable_streaming(gcode_viewer_);

        // Apply render mode - priority: cmdline > env var > settings
        const auto* config = get_runtime_config();
        const char* env_mode = std::getenv("HELIX_GCODE_MODE");

        if (config && config->gcode_render_mode >= 0) {
            auto render_mode = static_cast<helix::GcodeViewerRenderMode>(config->gcode_render_mode);
            ui_gcode_viewer_set_render_mode(gcode_viewer_, render_mode);
            spdlog::debug("[DetailView] Set G-code render mode: {} (cmdline)",
                          config->gcode_render_mode);
        } else if (env_mode) {
            spdlog::debug("[DetailView] G-code render mode: {} (env var)",
                          ui_gcode_viewer_is_using_2d_mode(gcode_viewer_) ? "2D" : "3D");
        } else {
            int render_mode_val = DisplaySettingsManager::instance().get_gcode_render_mode();
            if (render_mode_val == 3) {
                // Thumbnail Only mode - skip render mode setup, viewer won't be used
                spdlog::debug("[DetailView] G-code render mode: Thumbnail Only (settings)");
            } else {
                auto render_mode = static_cast<helix::GcodeViewerRenderMode>(render_mode_val);
                ui_gcode_viewer_set_render_mode(gcode_viewer_, render_mode);
                spdlog::debug("[DetailView] Set G-code render mode: {} (settings)",
                              render_mode_val);
            }
        }

        // Vertical offset to match thumbnail positioning
        ui_gcode_viewer_set_content_offset_y(gcode_viewer_, -0.10f);

        // Start paused — will resume in on_activate()
        ui_gcode_viewer_set_paused(gcode_viewer_, true);

        // Memory-pressure responder calls ui_gcode_viewer_clear_all_active();
        // flip the mode subject back to thumbnail so the user sees the slicer
        // preview rather than a transparent rectangle.
        ui_gcode_viewer_set_clear_callback(
            gcode_viewer_,
            [](lv_obj_t*, void* ud) {
                auto* self = static_cast<PrintSelectDetailView*>(ud);
                self->show_gcode_viewer(false);
                self->gcode_loaded_ = false;
            },
            this);
    }

    // The pre-print option rows are populated dynamically from the active
    // printer's PrePrintOptionSet — see populate_option_rows(). The
    // hardcoded checkbox widgets that used to live in the XML are gone;
    // their pointers exist only as inert nullptr fields kept on the class
    // for backward compatibility with external callers (none today).
    bed_mesh_checkbox_ = nullptr;
    qgl_checkbox_ = nullptr;
    z_tilt_checkbox_ = nullptr;
    nozzle_clean_checkbox_ = nullptr;
    purge_line_checkbox_ = nullptr;
    timelapse_checkbox_ = nullptr;
    pre_print_options_container_ =
        lv_obj_find_by_name(overlay_root_, "pre_print_options_container");

    // Look up the color swatches row container (parent card visibility is
    // driven by the color_swatches_visible subject — no flag manipulation here).
    color_swatches_row_ = lv_obj_find_by_name(overlay_root_, "color_swatches_row");

    // Make the color-requirements card tappable so the U1's visible swatches
    // open the native remap modal — matching the AFC/CFS whole-card click in
    // FilamentMappingCard. The card's children already declare
    // clickable=false + event_bubble=true in print_file_detail.xml (L071), so
    // the parent receives the click. lv_obj_add_event_cb on the card mirrors the
    // sibling FilamentMappingCard pattern (allowed exception). The handler gates
    // on the active backend's remap strategy at click time: it only opens the
    // modal for SnapmakerNative — on other backends this card is informational
    // and a different remap path applies, so the tap is a no-op.
    color_requirements_card_ = lv_obj_find_by_name(overlay_root_, "color_requirements_card");
    if (color_requirements_card_) {
        lv_obj_add_flag(color_requirements_card_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(
            color_requirements_card_,
            [](lv_event_t* e) {
                auto* self = static_cast<PrintSelectDetailView*>(lv_event_get_user_data(e));
                self->on_color_card_clicked();
            },
            LV_EVENT_CLICKED, this);
    }

    // Look up and initialize filament mapping card
    lv_obj_t* mapping_card = lv_obj_find_by_name(overlay_root_, "filament_mapping_card");
    lv_obj_t* mapping_rows = lv_obj_find_by_name(overlay_root_, "filament_mapping_rows");
    lv_obj_t* mapping_warning = lv_obj_find_by_name(overlay_root_, "filament_mapping_warning");
    filament_mapping_card_.create(mapping_card, mapping_rows, mapping_warning);
    // Route the card tap to the panel's single remap opener instead of the
    // card's own internal modal, so there is ONE opener and ONE modal instance
    // for every backend (AFC/CFS card tap, U1 swatch tap, preflight "Remap…"
    // all reach PrintSelectPanel::open_remap_modal()). on_remap_requested_ is
    // wired by the panel in create_detail_view() right after construction, so the
    // null check is just defensive — a tap before wiring is a no-op, not a crash.
    filament_mapping_card_.set_on_tap([this]() {
        if (on_remap_requested_) {
            on_remap_requested_();
        }
    });
    filament_mapping_card_.set_on_mappings_changed([this]() {
        // The card already refreshed its own slot/mapping state from the user's
        // edit — just re-color + re-gate. set_mappings() fires this synchronously
        // on the main thread, so a direct call is safe.
        refresh_preview_colors_and_mismatch();
    });

    // Look up history status display
    history_status_row_ = lv_obj_find_by_name(overlay_root_, "history_status_row");
    history_status_icon_ = lv_obj_find_by_name(overlay_root_, "history_status_icon");
    history_status_label_ = lv_obj_find_by_name(overlay_root_, "history_status_label");

    // Initialize print preparation manager (only if not already created —
    // survives destroy-on-close so callbacks set by PrintSelectPanel persist)
    if (!prep_manager_) {
        prep_manager_ = std::make_unique<PrintPreparationManager>();
    }

    spdlog::debug("[DetailView] Detail view created");
    return overlay_root_;
}

void PrintSelectDetailView::set_dependencies(IMoonrakerAPI* api, PrinterState* printer_state) {
    api_ = api;
    printer_state_ = printer_state;

    if (prep_manager_) {
        prep_manager_->set_dependencies(api_, printer_state_);
        // Per-option toggle state flows through the OptionStateProvider that
        // populate_option_rows() registers with the prep manager — no need to
        // wire individual legacy state/visibility subjects here.
    }
}

// ============================================================================
// Visibility
// ============================================================================

void PrintSelectDetailView::show(const std::string& filename, const std::string& current_path,
                                 const std::string& filament_type,
                                 const std::vector<std::string>& filament_colors,
                                 const std::vector<std::string>& filament_materials,
                                 size_t file_size_bytes) {
    // Lazy re-create widget tree if it was destroyed by destroy-on-close
    if (!overlay_root_ && parent_screen_) {
        spdlog::info("[DetailView] Re-creating widget tree (destroy-on-close recovery)");
        if (!create(parent_screen_)) {
            spdlog::error("[DetailView] Failed to re-create detail view");
            return;
        }
        // Re-wire dependencies (subjects need re-binding to new widgets)
        if (api_ || printer_state_) {
            set_dependencies(api_, printer_state_);
        }
    }

    if (!overlay_root_) {
        spdlog::warn("[DetailView] Cannot show: widget not created");
        return;
    }

    // Cache parameters for on_activate() to use
    current_filename_ = filename;
    current_path_ = current_path;
    current_filament_type_ = filament_type;
    current_filament_colors_ = filament_colors;
    current_filament_materials_ = filament_materials;
    current_file_size_bytes_ = file_size_bytes;

    // Clear cached metadata when file selection changes — the new async fetch will repopulate it
    cached_file_metadata_.reset();

    // Clear any used-tools filter carried over from the previously-selected
    // file BEFORE the pre-parse update() below. The card is repopulated from
    // this file's full palette and must show all tools until this file's own
    // gcode parse pushes its real used set (viewer-parse or headless hook).
    // Without this reset a disjoint prior set would mis-filter — or blank — the
    // new file's card pre-parse.
    filament_mapping_card_.set_used_tools(std::nullopt);

    // Update filament mapping card (shown when AMS is available)
    filament_mapping_card_.update(filament_colors, filament_materials);

    // Publish the mapping-card display subjects. The mapping card's visibility
    // depends only on its own state (AMS presence + slicer colors), so it can
    // be decided here. The swatches card, by contrast, must reflect the real
    // per-tool set used by the file — which is only known once the gcode is
    // parsed.
    const bool mapping_visible = filament_mapping_card_.should_show();
    lv_subject_set_int(&filament_mapping_visible_, mapping_visible ? 1 : 0);

    // Swatches start in a neutral "not yet known" state: hidden, no warning.
    // Seeding from the slicer palette index here mislabels chips (a T0+T2 file
    // renders as T0/T1) and inspects the wrong AMS slots. The authoritative
    // render happens in try_extract_gcode_colors() once the gcode viewer has
    // parsed and produced tools_used_indices. Reset every show() so re-selecting
    // a different file never leaks stale swatch state.
    //
    // filament_mismatch_ is likewise neutral-until-parse: seeding it from
    // filament_mapping_card_.has_mismatch() here would flash a value computed
    // against the full slicer palette before the validator runs against the
    // precise tools_used set. The pre-flight validator in
    // try_extract_gcode_colors() is the sole authoritative post-parse writer.
    lv_subject_set_int(&color_swatches_visible_, 0);
    lv_subject_set_int(&empty_tools_warning_, 0);
    lv_subject_set_int(&filament_mismatch_, 0);
    lv_subject_set_int(&detail_prefer_sliced_colors_, 0); // every open starts on actual colors

    // Drop any cached pre-flight result from a previously-selected file. The
    // validator re-runs in try_extract_gcode_colors() once this file's gcode is
    // parsed; clearing here prevents the gate/modal from reading stale checks.
    preflight_result_ = {};

    // Drop any pending run_when_loaded() callback from a previously-selected
    // file so a stale print-attempt can't fire against this file's parse.
    on_loaded_cb_ = nullptr;

    // Reset headless-scan state for the newly-selected file. on_activate() will
    // (re)kick the scan. Drop any pending preflight-ready attempt + timer so a
    // stale attempt from a previous file can't fire against this one.
    headless_tools_used_.reset();
    headless_scan_done_ = false;
    on_preflight_ready_cb_ = nullptr;
    if (preflight_ready_timeout_timer_) {
        lv_timer_delete(preflight_ready_timeout_timer_);
        preflight_ready_timeout_timer_ = nullptr;
    }

    // Register with NavigationManager for lifecycle callbacks
    NavigationManager::instance().register_overlay_instance(overlay_root_, this);

    // Register close callback to destroy widget tree when overlay closes.
    // Frees memory when detail view is dismissed. Subjects survive;
    // next show() call re-creates widgets via lazy creation above.
    NavigationManager::instance().register_overlay_close_callback(
        overlay_root_, [this]() { destroy_overlay_ui(overlay_root_); });

    // Push onto navigation stack - on_activate() will be called by NavigationManager
    NavigationManager::instance().push_overlay(overlay_root_);

    if (visible_subject_) {
        lv_subject_set_int(visible_subject_, 1);
    }

    spdlog::debug("[DetailView] Showing detail view for: {} ({} colors)", filename,
                  filament_colors.size());
}

void PrintSelectDetailView::hide() {
    if (!overlay_root_) {
        return;
    }

    // Pop from navigation stack - on_deactivate() will be called by NavigationManager
    NavigationManager::instance().go_back();

    if (visible_subject_) {
        lv_subject_set_int(visible_subject_, 0);
    }

    spdlog::debug("[DetailView] Detail view hidden");
}

// ============================================================================
// Lifecycle Hooks (called by NavigationManager)
// ============================================================================

void PrintSelectDetailView::on_activate() {
    // Call base class first
    OverlayBase::on_activate();

    spdlog::debug("[DetailView] on_activate() for file: {}", current_filename_);

    // (Re)build dynamic option rows from the active printer's option set.
    // Idempotent — only rebuilds when the printer type has changed.
    populate_option_rows();

    // Cache file size for safety checks (before modification attempts)
    if (prep_manager_ && current_file_size_bytes_ > 0) {
        prep_manager_->set_cached_file_size(current_file_size_bytes_);
    }

    // Ask the active AMS backend to refresh its slot/state view. Lets users
    // self-recover from any drift between cached UI state and printer truth
    // by navigating away and back. Default backend impl is a no-op; AD5X IFS
    // re-reads Adventurer5M.json + GET_ZCOLOR. Debounced internally.
    if (auto* backend = AmsState::instance().get_backend()) {
        backend->request_resync();
    }

    // Trigger async scan for embedded G-code operations (for conflict detection)
    // The scan happens NOW after registration, so if user navigates away,
    // on_deactivate() will be called and we can check cleanup_called()
    if (!current_filename_.empty() && prep_manager_) {
        prep_manager_->scan_file_for_operations(current_filename_, current_path_);
    }

    // Headless tools_used scan — runs on ALL platforms (including 2D-only, where
    // the visual viewer below skips parsing). Provides tools_used + the pre-flight
    // readiness signal the print-start gate waits on, so prints never hang on
    // 2D-only devices. Result is typically ready by the time Print is tapped.
    kick_off_headless_tools_scan();

    // Invalidate predictor cache so we pick up any new timing data from completed prints
    if (prep_manager_) {
        prep_manager_->invalidate_predictor_cache();
    }

    // Calculate initial pre-print time estimate
    update_prep_time_label();

    // Load gcode for 3D/2D preview (viewer stays paused until load completes)
    load_gcode_for_preview();
}

void PrintSelectDetailView::on_deactivate() {
    spdlog::debug("[DetailView] on_deactivate()");

    // Clear and pause gcode viewer immediately so the old model doesn't
    // linger when the user selects a different file
    if (gcode_viewer_) {
        ui_gcode_viewer_clear(gcode_viewer_);
        ui_gcode_viewer_set_paused(gcode_viewer_, true);
    }

    // Reset viewer mode to thumbnail so next open starts clean
    show_gcode_viewer(false);
    lv_subject_set_int(&detail_viewer_first_frame_, 0);
    gcode_loaded_ = false;

    // Drop any pending run_when_loaded() callback. If the user tapped Print
    // before parse completed (deferring the attempt) and then navigated away,
    // a late load callback firing fire_on_loaded() would call start_print() on
    // a hidden panel → a ghost print with no UI. Clearing here prevents that.
    on_loaded_cb_ = nullptr;

    // Same protection for the preflight-ready deferral + its safety timer: a late
    // scan completion (or timeout) must not start a print on a hidden panel.
    on_preflight_ready_cb_ = nullptr;
    if (preflight_ready_timeout_timer_) {
        lv_timer_delete(preflight_ready_timeout_timer_);
        preflight_ready_timeout_timer_ = nullptr;
    }

    // Hide any open delete confirmation modal
    hide_delete_confirmation();

    // Note: We don't cancel scans here because PrintPreparationManager
    // has its own lifetime guard. Async callbacks in prep_manager_
    // will check cleanup_called() if needed.

    // Call base class
    OverlayBase::on_deactivate();
}

void PrintSelectDetailView::cleanup() {
    spdlog::debug("[DetailView] cleanup()");

    // Pause viewer before subject cleanup to avoid rendering with freed subjects.
    // Drop the first-frame callback too: it captures `this` and writes
    // detail_viewer_first_frame_, which subjects_.deinit_all() below destroys.
    // The viewer outlives this object on some teardown paths, and paused
    // rendering is not a guarantee — unpausing anywhere else would resurrect it.
    if (gcode_viewer_) {
        ui_gcode_viewer_set_paused(gcode_viewer_, true);
        ui_gcode_viewer_set_first_frame_callback(gcode_viewer_, nullptr, nullptr);
    }

    // Expire all outstanding async tokens
    lifetime_.invalidate();

    // Unregister from NavigationManager before cleaning up
    if (overlay_root_) {
        NavigationManager::instance().unregister_overlay_instance(overlay_root_);
    }

    // Deinitialize subjects to disconnect observers
    if (subjects_initialized_) {
        subjects_.deinit_all();
        subjects_initialized_ = false;
    }

    // Call base class to set cleanup_called_ flag
    OverlayBase::cleanup();
}

// ============================================================================
// Destroy-on-close support
// ============================================================================

void PrintSelectDetailView::on_ui_destroyed() {
    spdlog::debug("[DetailView] on_ui_destroyed() - nulling widget pointers");

    // Invalidate outstanding tokens so in-flight async callbacks (gcode download,
    // metadata fetch, load callbacks) bail out — they captured pointers to
    // widgets that no longer exist (e.g. gcode_viewer_).
    // New tokens from lifetime_.token() will be valid for the next create() cycle.
    lifetime_.invalidate();

    // Pause and clear gcode viewer state (widget is already deleted by base)
    gcode_loaded_ = false;

    // Drop any pending run_when_loaded() callback so a late load callback can't
    // fire start_print() against a destroyed view (ghost-print guard).
    on_loaded_cb_ = nullptr;

    // Clean up temp gcode file so stale cached data doesn't persist
    if (!temp_gcode_path_.empty()) {
        std::remove(temp_gcode_path_.c_str());
        temp_gcode_path_.clear();
    }

    // Null all child widget pointers (widget tree already deleted by base class)
    // Note: parent_screen_ is NOT nulled — it's the parent screen (not a child
    // widget) and is needed for lazy re-creation in show().
    confirmation_dialog_widget_ = nullptr;
    print_button_ = nullptr;
    gcode_viewer_ = nullptr;

    // Pre-print option checkboxes (kept as inert fields; see create()).
    bed_mesh_checkbox_ = nullptr;
    qgl_checkbox_ = nullptr;
    z_tilt_checkbox_ = nullptr;
    nozzle_clean_checkbox_ = nullptr;
    purge_line_checkbox_ = nullptr;
    timelapse_checkbox_ = nullptr;

    // The dynamic option rows were children of overlay_root_, which has been
    // destroyed by the base class. Drop the renderer's row state and force a
    // rebuild on next show(). Subjects inside the renderer are heap-owned —
    // their observers were attached to the now-deleted row widgets, so
    // dropping the subjects here is safe.
    pre_print_options_container_ = nullptr;
    option_rows_renderer_.clear();
    last_rendered_printer_type_.clear();
    if (prep_manager_) {
        prep_manager_->set_option_state_provider(nullptr);
    }

    color_swatches_row_ = nullptr;
    color_requirements_card_ = nullptr;

    // Filament mapping card
    filament_mapping_card_.on_ui_destroyed();

    // History status display
    history_status_row_ = nullptr;
    history_status_icon_ = nullptr;
    history_status_label_ = nullptr;

    // Note: prep_manager_ is NOT reset — it holds no widget references and
    // retains its callbacks (scan_complete, macro_analysis) set by PrintSelectPanel.
    // It will be re-wired with dependencies in show() -> set_dependencies().
}

// ============================================================================
// Delete Confirmation
// ============================================================================

void PrintSelectDetailView::show_delete_confirmation(const std::string& filename) {
    // Create message with current filename
    char msg_buf[256];
    snprintf(msg_buf, sizeof(msg_buf),
             "Are you sure you want to delete '%s'? This action cannot be undone.",
             filename.c_str());

    confirmation_dialog_widget_ = helix::ui::modal_show_confirmation(
        lv_tr("Delete File?"), msg_buf, ModalSeverity::Warning, lv_tr("Delete"),
        on_confirm_delete_static, on_cancel_delete_static, this);

    if (!confirmation_dialog_widget_) {
        spdlog::error("[DetailView] Failed to create confirmation dialog");
        return;
    }

    spdlog::info("[DetailView] Delete confirmation dialog shown for: {}", filename);
}

void PrintSelectDetailView::hide_delete_confirmation() {
    if (confirmation_dialog_widget_) {
        helix::ui::modal_hide(confirmation_dialog_widget_);
        confirmation_dialog_widget_ = nullptr;
    }
}

// ============================================================================
// Resize Handling
// ============================================================================

void PrintSelectDetailView::handle_resize(lv_obj_t* parent_screen) {
    if (!overlay_root_ || !parent_screen) {
        return;
    }

    lv_obj_t* content_container = lv_obj_find_by_name(overlay_root_, "content_container");
    if (content_container) {
        lv_coord_t padding = ui_get_header_content_padding();
        lv_obj_set_style_pad_all(content_container, padding, 0);
    }
}

// ============================================================================
// Internal Methods
// ============================================================================

void PrintSelectDetailView::on_confirm_delete_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectDetailView*>(lv_event_get_user_data(e));
    if (self) {
        self->hide_delete_confirmation();
        if (self->on_delete_confirmed_) {
            self->on_delete_confirmed_();
        }
    }
}

void PrintSelectDetailView::on_cancel_delete_static(lv_event_t* e) {
    auto* self = static_cast<PrintSelectDetailView*>(lv_event_get_user_data(e));
    if (self) {
        self->hide_delete_confirmation();
    }
}

void PrintSelectDetailView::update_color_swatches(const std::set<int>& tool_indices,
                                                  const std::vector<std::string>& palette_colors) {
    // TWO-TONE chip: the TOP band shows the GCODE FILE intended color (slicer
    // palette) + the gcode tool number ("T0", "T2"); the BOTTOM band shows the
    // ACTUAL present color of the EFFECTIVE mapped slot + that slot's lane
    // number ("1", "4"). A thin divider separates them. We source the mapped
    // slot from the resolved tool→slot mapping (NOT backend->get_slot_info(),
    // since slot != tool on multi-unit backends like the U1). This mirrors
    // recompute_preflight() / open_remap_modal: use the card's current mappings
    // if present (so chips reflect a user edit after Done), else compute the
    // defaults — the same effective mapping the remap dialog shows.
    //
    // No-backend / palette-only: collect_available_slots() is empty and
    // compute_defaults yields mapped_slot < 0, so no lane resolves — the bottom
    // band stays blank and the chip is effectively just the gcode-color top band.
    if (!color_swatches_row_) {
        return;
    }

    helix::ui::safe_clean_children(color_swatches_row_);

    const auto tools = get_used_tool_info(); // real tool_index, intended colors
    auto slots = AmsState::instance().collect_available_slots();
    // Effective (toggle-aware) mapping: card edits win on editable backends;
    // U1/ACE auto-match so the bottom band shows the matched lane, not identity.
    auto mappings = effective_mappings();

    const lv_color_t neutral = theme_manager_get_color("text_muted");

    for (int tool : tool_indices) {
        // Resolve this tool's effective mapping → present slot.
        const helix::AvailableSlot* resolved = nullptr;
        for (const auto& m : mappings) {
            if (m.tool_index != tool) {
                continue;
            }
            if (!m.is_auto && m.mapped_slot >= 0) {
                for (const auto& s : slots) {
                    if (s.slot_index == m.mapped_slot && s.backend_index == m.mapped_backend) {
                        resolved = &s;
                        break;
                    }
                }
            }
            break;
        }

        // TOP band: gcode intended color from the slicer palette. Falls back to
        // the neutral chip default when the palette has no entry for this tool.
        lv_color_t gcode_color = neutral;
        if (tool >= 0 && static_cast<size_t>(tool) < palette_colors.size() &&
            !palette_colors[tool].empty()) {
            gcode_color = theme_manager_parse_hex_color(palette_colors[tool].c_str());
        }

        // BOTTOM band: present color of the effective mapped slot.
        lv_color_t slot_color = neutral;
        bool have_slot_color = false;
        bool slot_is_empty = false;
        std::string slot_number_text;

        if (resolved) {
            slot_number_text = std::to_string(resolved->local_slot_index + 1);
            if (resolved->is_empty) {
                // Mapped to an empty lane — show which lane it routes to but
                // render the empty-slot variant on the bottom band.
                slot_is_empty = true;
            } else {
                slot_color = lv_color_hex(resolved->color_rgb);
                have_slot_color = true;
            }
        }
        // No resolved lane (auto/unmapped or no backend): bottom band stays
        // blank (no number, default fill) — chip reads as just the gcode top.

        auto* swatch =
            static_cast<lv_obj_t*>(lv_xml_create(color_swatches_row_, "filament_swatch", nullptr));
        if (!swatch) {
            continue;
        }
        // Fix the chip width in code: a numeric width on the component <view>
        // root is not honored by lv_xml_create (only "content"/"%"), and the
        // band labels use flex_grow (which contributes 0 to content-width), so
        // without an explicit width the whole chip collapses to 0. A uniform
        // width lets both bands cross-stretch to the same width (connected,
        // full-bleed) and centers the labels. Height comes from the 32px parent
        // row via the XML height="100%". Tunable.
        lv_obj_set_width(swatch, 40);

        // Top band fill + label.
        if (auto* top_band = lv_obj_find_by_name(swatch, "top_band")) {
            lv_obj_set_style_bg_color(top_band, gcode_color, 0);
            if (auto* tool_label = lv_obj_find_by_name(top_band, "tool_label")) {
                lv_label_set_text_fmt(tool_label, "T%d", tool);
                lv_obj_set_style_text_color(tool_label,
                                            theme_manager_get_contrast_color(gcode_color), 0);
            }
        }

        // Bottom band fill (or empty variant) + lane number.
        lv_color_t bottom_text_color = slot_is_empty ? theme_manager_get_color("warning")
                                                     : theme_manager_get_contrast_color(slot_color);
        if (auto* bottom_band = lv_obj_find_by_name(swatch, "bottom_band")) {
            if (slot_is_empty) {
                lv_obj_add_state(bottom_band, LV_STATE_USER_1);
            } else if (have_slot_color) {
                lv_obj_set_style_bg_color(bottom_band, slot_color, 0);
            }
            if (auto* slot_label = lv_obj_find_by_name(bottom_band, "slot_label")) {
                lv_label_set_text(slot_label, slot_number_text.c_str());
                lv_obj_set_style_text_color(slot_label, bottom_text_color, 0);
            }
        }

        // Divider: a color that reads against BOTH band fills. Blend the two
        // band colors (50/50) and take the contrast of the blend, so the rule
        // stays visible whether the bands are light, dark, or mixed.
        if (auto* divider = lv_obj_find_by_name(swatch, "divider")) {
            lv_color_t blend = lv_color_mix(gcode_color, slot_color, LV_OPA_50);
            lv_obj_set_style_bg_color(divider, theme_manager_get_contrast_color(blend), 0);
        }
    }

    // Note: empty_tools_warning_ is published by the pre-flight validator in
    // try_extract_gcode_colors() (the single source of truth), NOT here — this
    // method only renders swatches.
}

void PrintSelectDetailView::update_history_status(FileHistoryStatus status, int success_count) {
    if (!history_status_row_ || !history_status_icon_ || !history_status_label_) {
        return;
    }

    switch (status) {
    case FileHistoryStatus::NEVER_PRINTED:
        // Hide the row entirely for files with no history
        lv_obj_add_flag(history_status_row_, LV_OBJ_FLAG_HIDDEN);
        break;

    case FileHistoryStatus::CURRENTLY_PRINTING:
        lv_obj_remove_flag(history_status_row_, LV_OBJ_FLAG_HIDDEN);
        ui_icon_set_source(history_status_icon_, "clock");
        ui_icon_set_variant(history_status_icon_, "accent");
        lv_label_set_text(history_status_label_, lv_tr("Currently printing"));
        break;

    case FileHistoryStatus::COMPLETED: {
        lv_obj_remove_flag(history_status_row_, LV_OBJ_FLAG_HIDDEN);
        ui_icon_set_source(history_status_icon_, "check");
        ui_icon_set_variant(history_status_icon_, "success");
        // Format: "Printed N time(s)"
        char buf[64];
        snprintf(buf, sizeof(buf),
                 lv_tr(success_count == 1 ? "Printed %d time" : "Printed %d times"), success_count);
        lv_label_set_text(history_status_label_, buf);
        break;
    }

    case FileHistoryStatus::FAILED:
        lv_obj_remove_flag(history_status_row_, LV_OBJ_FLAG_HIDDEN);
        ui_icon_set_source(history_status_icon_, "alert");
        ui_icon_set_variant(history_status_icon_, "error");
        lv_label_set_text(history_status_label_, lv_tr("Last print failed"));
        break;

    case FileHistoryStatus::CANCELLED:
        lv_obj_remove_flag(history_status_row_, LV_OBJ_FLAG_HIDDEN);
        ui_icon_set_source(history_status_icon_, "cancel");
        ui_icon_set_variant(history_status_icon_, "warning");
        lv_label_set_text(history_status_label_, lv_tr("Last print cancelled"));
        break;
    }
}

// ============================================================================
// G-code Viewer
// ============================================================================

void PrintSelectDetailView::show_gcode_viewer(bool show) {
    // detail_gcode_viewer_mode_ drives the XML visibility bindings in
    // print_file_detail.xml:
    //   0 = thumbnail  (viewer hidden, gradient + thumbnail shown)
    //   1 = 3D viewer  (viewer shown, gradient hidden for clean black bg,
    //                   rotate hint shown)
    //   2 = 2D viewer  (viewer shown over the gradient, rotate hint hidden —
    //                   there's no rotate affordance for the flat 2D layer view)
    // 2D-mode devices (no-GLES / GPU-blocked / budget-forced) now render the
    // real toolpath preview instead of falling back to the thumbnail.
    int mode = 0;
    if (show) {
        const bool is_2d = gcode_viewer_ && ui_gcode_viewer_is_using_2d_mode(gcode_viewer_);
        mode = is_2d ? 2 : 1;
    }
    lv_subject_set_int(&detail_gcode_viewer_mode_, mode);

    // Returning to thumbnail mode must also drop the first-frame latch — that
    // latch is what keeps the thumbnail hidden once the viewer has painted
    // (print_file_detail.xml binds detail_viewer_first_frame). Leaving it set
    // while hiding the viewer (the memory-pressure clear callback does exactly
    // that, mid-view) hides BOTH layers and the preview goes blank.
    if (mode == 0) {
        lv_subject_set_int(&detail_viewer_first_frame_, 0);
    }

    // The 3D render is the preview when the viewer is active, so the
    // no-thumbnail placeholder glyph must not sit on top of it. (When the
    // viewer is inactive the print-select panel's has-thumbnail logic owns
    // whether the placeholder shows.)
    if (mode > 0 && overlay_root_) {
        lv_obj_t* no_thumb = lv_obj_find_by_name(overlay_root_, "detail_no_thumbnail_icon");
        if (no_thumb) {
            lv_obj_add_flag(no_thumb, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Hide loading spinner now that viewer state is resolved
    lv_subject_set_int(&detail_gcode_loading_, 0);

    spdlog::trace("[DetailView] G-code viewer mode: {} ({})", mode, mode == 0 ? "thumbnail" : "3D");
}

void PrintSelectDetailView::apply_preview_colors() {
    if (!gcode_viewer_ || !gcode_loaded_) {
        return;
    }
    const auto tools = get_used_tool_info();
    if (tools.empty()) {
        return;
    }
    const auto slots = AmsState::instance().collect_available_slots();

    // Single color engine — the SAME FilamentMapper::effective_tool_colors the
    // print-file swatches and the live print-status render use. The sliced/actual
    // toggle only changes which MAPPINGS feed it:
    //   - Actual (loaded): effective_mappings() — card edits win on editable
    //     backends, auto color+type match otherwise. This is what colors each
    //     tool by its MATCHED lane instead of the identity physical-slot position.
    //   - Sliced: fully-default (unmapped) mappings, so resolve_display_colors
    //     falls back to each tool's own slicer color — the file's intended look.
    const std::vector<helix::ToolMapping> mappings =
        (lv_subject_get_int(&detail_prefer_sliced_colors_) == 1)
            ? std::vector<helix::ToolMapping>(tools.size())
            : effective_mappings();

    const auto colors = helix::FilamentMapper::effective_tool_colors(tools, mappings, slots);
    if (!colors.empty()) {
        ui_gcode_viewer_set_tool_colors(gcode_viewer_, colors);
        lv_obj_invalidate(gcode_viewer_);
    }
}

void PrintSelectDetailView::set_prefer_sliced_colors(bool prefer_sliced) {
    lv_subject_set_int(&detail_prefer_sliced_colors_, prefer_sliced ? 1 : 0);
    apply_preview_colors();
    if (gcode_viewer_) {
        lv_obj_invalidate(gcode_viewer_);
    }
}

void PrintSelectDetailView::on_ams_state_changed() {
    // Cheap guard: no work while closed / not yet loaded. on_deactivate() clears
    // gcode_loaded_ and on_ui_destroyed() nulls gcode_viewer_, so this also
    // protects against a dangling viewer pointer after the view is torn down.
    if (!is_visible() || !gcode_loaded_ || !gcode_viewer_) {
        return;
    }
    // Refresh loaded slot colors WITHOUT recomputing mappings (preserve remap).
    filament_mapping_card_.refresh_slot_data();
    refresh_preview_colors_and_mismatch();
}

void PrintSelectDetailView::refresh_preview_colors_and_mismatch() {
    apply_preview_colors();
    lv_subject_set_int(&filament_mismatch_, filament_mapping_card_.has_mismatch() ? 1 : 0);
    // Re-evaluate the pre-flight gate so a subsequent Print reflects the current
    // tool->slot mapping (native remap flow reads get_filament_mappings()).
    recompute_preflight();
    // Re-render the FILAMENTS chips so slot number + present color track the
    // current mapping/slot state.
    if (lv_subject_get_int(&color_swatches_visible_) == 1) {
        update_color_swatches(tools_used_effective(), current_filament_colors_);
    }
}

void PrintSelectDetailView::try_extract_gcode_colors(lv_obj_t* viewer) {
    auto* parsed = ui_gcode_viewer_get_parsed_file(viewer);
    if (!parsed) {
        return;
    }

    // Backfill filament_colors when slicer metadata didn't provide them
    // (Snapmaker and a few other Moonraker variants).
    if (current_filament_colors_.empty() && !parsed->tool_color_palette.empty()) {
        spdlog::info(
            "[DetailView] Metadata lacked filament colors — extracted {} from parsed gcode",
            parsed->tool_color_palette.size());
        current_filament_colors_ = parsed->tool_color_palette;

        // Rebuild the mapping card's internal tool/slot/mapping state with the
        // newly-extracted colors (the card uses them when AMS is present). The
        // filament_mismatch_ / empty_tools_warning_ subjects are NOT published
        // here — the pre-flight validator below is the single source of truth.
        filament_mapping_card_.update(current_filament_colors_, current_filament_materials_);
    }

    // Re-publish swatches/mapping visibility using the precise tools_used set
    // from parsed gcode — not the slicer palette size (which often over-counts).
    const bool mapping_visible = filament_mapping_card_.should_show();
    const bool swatches_visible =
        !mapping_visible && swatches_card_visible_for(parsed->tools_used_indices.size());
    lv_subject_set_int(&filament_mapping_visible_, mapping_visible ? 1 : 0);
    lv_subject_set_int(&color_swatches_visible_, swatches_visible ? 1 : 0);

    if (swatches_visible) {
        update_color_swatches(parsed->tools_used_indices, current_filament_colors_);
    }

    // Backend-agnostic pre-flight validation (single source of truth for
    // filament_mismatch_ + empty_tools_warning_). Extracted so the native
    // remap flow can re-evaluate the gate after the backend mapping changes.
    recompute_preflight();

    // Restrict the mapping card to the tools this file actually uses. The card
    // was populated from the full slicer palette; now that the viewer has parsed
    // we know the real used set. Empty/unknown ⇒ show all (safe default).
    filament_mapping_card_.set_used_tools(tools_used_effective());
}

std::vector<helix::GcodeToolInfo> PrintSelectDetailView::get_used_tool_info() const {
    // Source per-tool info DIRECTLY from the slicer palette (Moonraker metadata,
    // populated on ALL platforms) via the stateless assembler — NOT from the
    // mapping card INSTANCE, whose tool_info_ is empty on the U1/headless path.
    const auto all_tool_info =
        FilamentMappingCard::build_tool_info(current_filament_colors_, current_filament_materials_);
    const std::set<int> used = tools_used_effective();

    std::vector<helix::GcodeToolInfo> tools;
    tools.reserve(used.size());
    for (int tool : used) {
        if (tool >= 0 && static_cast<size_t>(tool) < all_tool_info.size()) {
            auto info = all_tool_info[static_cast<size_t>(tool)];
            info.tool_index = tool; // real gcode tool number, not palette ordinal
            tools.push_back(info);
        }
    }
    return tools;
}

bool PrintSelectDetailView::effective_auto_match() const {
    // Non-editable-card backends (U1 / ACE) have no card UI to flip the
    // persisted auto-color preference, so they always auto-match (color+type);
    // otherwise the persisted default (FALSE) would force positional matching
    // and pick the wrong lane. Editable backends honor the user's setting.
    bool card_editable = false;
    if (auto* backend = AmsState::instance().get_backend()) {
        card_editable = backend->get_tool_mapping_capabilities().editable;
    }
    return !card_editable || SettingsManager::instance().get_auto_color_map();
}

std::vector<helix::ToolMapping> PrintSelectDetailView::effective_mappings() const {
    // Editable backends: the card seeds and owns mappings_, and user edits win.
    auto m = filament_mapping_card_.get_mappings();
    if (!m.empty()) {
        return m;
    }
    // Non-editable backends (U1 / ACE): the card is hidden and get_mappings() is
    // empty — resolve the effective (toggle-aware) mapping the same way the live
    // render does, so swatches + preflight + render all agree.
    return helix::FilamentMapper::effective_mappings(get_used_tool_info(),
                                                     AmsState::instance().collect_available_slots(),
                                                     effective_auto_match());
}

void PrintSelectDetailView::recompute_preflight() {
    // ------------------------------------------------------------------
    // Backend-agnostic pre-flight validation (single source of truth for
    // filament_mismatch_ + empty_tools_warning_).
    //
    // Runs for ALL AMS backends — including those whose mapping card is hidden
    // (Snapmaker U1 / ACE), where get_available_slots() on the card is empty.
    // We source slots straight from AmsState's canonical accessor, build the
    // intended per-tool color/material from the slicer palette (reusing the
    // mapping card's already-parsed tool_info_, filtered to the precise
    // tools_used set), compute default mappings, then validate.
    //
    // Guarded on tools_used availability: a no-op until EITHER the viewer has
    // parsed (full platforms) OR the headless scan has completed (2D-only) — both
    // feed tools_used_effective(). Before either there is nothing to validate.
    // ------------------------------------------------------------------
    const std::set<int> used = tools_used_effective();
    if (used.empty() && !headless_scan_done_) {
        // Nothing parsed yet and no headless result: defer. (An empty set AFTER a
        // completed headless scan is a legitimate single-extruder result and must
        // still validate, so we only bail when truly nothing is known.)
        return;
    }

    // Per-tool intent for the precise tools_used set, sourced directly from the
    // slicer palette (current_filament_colors_/materials) — the same data the
    // swatches use, populated on ALL platforms — NOT from the card instance's
    // tool_info_ (empty on the U1/headless path → the original "0 tools" bug).
    const auto tools = get_used_tool_info();

    auto slots = AmsState::instance().collect_available_slots();

    // Validate against the EFFECTIVE mapping the print will actually use, not a
    // freshly-recomputed default. The card's current mappings_ are what
    // PrintStartController::apply_remap() sends to the backend at print-start, so
    // the gate must consult the same vector — otherwise a native remap (AFC /
    // Happy Hare / CFS / AD5X-IFS / toolchanger) would never clear the block.
    //
    // Behavior-preserving at parse time: for editable backends the card seeds
    // mappings_ with the effective defaults until the user edits it (identical
    // result); for U1/ACE the card is hidden and mappings_ is empty, so
    // effective_mappings() resolves the toggle-aware (auto color+type) match —
    // the SAME mapping the color swatches and live render use.
    auto mapping = effective_mappings();
    preflight_result_ = helix::PreflightValidator::validate(tools, slots, mapping);

    bool any_mismatch = false;
    for (const auto& check : preflight_result_.checks) {
        if (check.severity != helix::ToolCheck::Severity::Ok) {
            any_mismatch = true;
            break;
        }
    }
    lv_subject_set_int(&filament_mismatch_, any_mismatch ? 1 : 0);
    lv_subject_set_int(&empty_tools_warning_, preflight_result_.has_block() ? 1 : 0);

    spdlog::debug("[DetailView] Preflight: {} tools, {} slots, {} checks, mismatch={}, block={}",
                  tools.size(), slots.size(), preflight_result_.checks.size(), any_mismatch,
                  preflight_result_.has_block());
}

std::set<int> PrintSelectDetailView::tools_used_effective() const {
    // Prefer the visual viewer's parsed set (full platforms): it carries the
    // single-extruder {0} convention from a color palette. Fall back to the
    // headless scan (2D-only platforms, where the viewer never parses).
    if (gcode_viewer_) {
        if (auto* parsed = ui_gcode_viewer_get_parsed_file(gcode_viewer_)) {
            if (!parsed->tools_used_indices.empty()) {
                return parsed->tools_used_indices;
            }
        }
    }
    if (headless_tools_used_) {
        return *headless_tools_used_;
    }
    return {};
}

std::set<int> PrintSelectDetailView::get_tools_used() const {
    return tools_used_effective();
}

std::map<int, int> PrintSelectDetailView::get_effective_remap() const {
    // default_head(t): the physical head a logical tool routes to with no remap.
    // Tools 0..3 map to their identity head; anything else falls back to head 0.
    auto default_head = [](int tool) { return (tool >= 0 && tool <= 3) ? tool : 0; };

    std::map<int, int> remap;
    // Source the mappings from effective_mappings() — the SAME toggle-aware match
    // the render, swatches, and pre-flight use. On editable backends this is the
    // card's mappings (user edits win); on non-editable backends (Snapmaker U1 /
    // ACE), where the card is hidden and get_mappings() is empty, it is the auto
    // color+type match. This is what turns the U1's emitted SET_PRINT_EXTRUDER_MAP
    // from an empty identity into the real per-tool routing (each logical tool to
    // the physical head holding its matched filament). mapped_slot is the physical
    // head 0..3 on the U1 (slot_index == head).
    for (const auto& m : effective_mappings()) {
        // Only include genuine remaps: a real slot assignment that differs from
        // the firmware-default head for this tool. Identity mappings are omitted
        // (the firmware already routes them).
        if (m.mapped_slot >= 0 && m.mapped_slot != default_head(m.tool_index)) {
            remap[m.tool_index] = m.mapped_slot;
        }
    }
    return remap;
}

void PrintSelectDetailView::set_filament_mappings(std::vector<helix::ToolMapping> mappings) {
    spdlog::debug("[DetailView] set_filament_mappings: {} mapping(s)", mappings.size());
    filament_mapping_card_.set_mappings(std::move(mappings));
}

void PrintSelectDetailView::open_filament_mapping_modal() {
    filament_mapping_card_.open_mapping_modal();
}

void PrintSelectDetailView::on_color_card_clicked() {
    // The color-requirements swatch card is the visible remap entry point on
    // backends whose editable FilamentMappingCard is hidden (e.g. Snapmaker U1).
    // Fire the panel's unified remap opener for ANY backend that supports remap
    // (strategy != None); the panel opener itself guards plugin presence etc.
    // On a non-remappable backend (None) the tap is a deliberate no-op.
    auto* backend = AmsState::instance().get_backend();
    if (!backend || backend->get_remap_strategy() == AmsBackend::RemapStrategy::None) {
        return;
    }
    spdlog::debug("[PrintSelect] swatch tap -> remap modal");
    if (on_remap_requested_) {
        on_remap_requested_();
    }
}

void PrintSelectDetailView::run_when_loaded(std::function<void()> cb) {
    if (!cb) {
        return;
    }
    // Already parsed: preflight_result_ is fresh, run synchronously (main thread).
    if (gcode_loaded_) {
        cb();
        return;
    }
    // Parse still in flight: store; fire_on_loaded() invokes it post-parse.
    on_loaded_cb_ = std::move(cb);
}

void PrintSelectDetailView::fire_on_loaded() {
    if (on_loaded_cb_) {
        auto cb = std::move(on_loaded_cb_);
        on_loaded_cb_ = nullptr;
        cb();
    }
}

void PrintSelectDetailView::run_when_preflight_ready(std::function<void()> cb) {
    if (!cb) {
        return;
    }
    // Already ready (viewer parsed or headless scan done): run synchronously.
    if (is_preflight_ready()) {
        cb();
        return;
    }
    on_preflight_ready_cb_ = std::move(cb);

    // Arm a one-shot safety timeout so a stuck/failed scan can never wedge the
    // print. On expiry we fire the deferred attempt anyway (graceful degradation:
    // print without Part A's optimization rather than never starting).
    if (preflight_ready_timeout_timer_) {
        lv_timer_delete(preflight_ready_timeout_timer_);
        preflight_ready_timeout_timer_ = nullptr;
    }
    preflight_ready_timeout_timer_ = lv_timer_create(
        [](lv_timer_t* t) {
            auto* self = static_cast<PrintSelectDetailView*>(lv_timer_get_user_data(t));
            spdlog::warn("[DetailView] Pre-flight readiness timed out — proceeding without "
                         "tools_used (graceful degradation)");
            // Mark done so a later readiness signal doesn't double-fire, and so
            // is_preflight_ready() returns true for the deferred re-entry.
            self->headless_scan_done_ = true;
            self->fire_on_preflight_ready();
        },
        PREFLIGHT_READY_TIMEOUT_MS, this);
    lv_timer_set_repeat_count(preflight_ready_timeout_timer_, 1);
}

void PrintSelectDetailView::fire_on_preflight_ready() {
    if (preflight_ready_timeout_timer_) {
        lv_timer_delete(preflight_ready_timeout_timer_);
        preflight_ready_timeout_timer_ = nullptr;
    }
    if (on_preflight_ready_cb_) {
        auto cb = std::move(on_preflight_ready_cb_);
        on_preflight_ready_cb_ = nullptr;
        cb();
    }
}

void PrintSelectDetailView::kick_off_headless_tools_scan() {
    headless_scan_done_ = false;
    headless_tools_used_.reset();

    if (!api_ || current_filename_.empty()) {
        // No way to scan — mark done with no result so the gate degrades to
        // "proceed without tools_used" instead of hanging.
        headless_scan_done_ = true;
        return;
    }

    const std::string file_path =
        current_path_.empty() ? current_filename_ : current_path_ + "/" + current_filename_;

    std::string cache_dir = get_helix_cache_dir("gcode_temp");
    if (cache_dir.empty()) {
        spdlog::warn("[DetailView] No cache dir for headless tools scan — degrading gracefully");
        headless_scan_done_ = true;
        return;
    }
    const std::string scan_path =
        cache_dir + "/tools_scan_" + std::to_string(std::hash<std::string>{}(file_path)) + ".gcode";

    auto tok = lifetime_.token();

    // Marshals the final state back to the main thread (LVGL + member writes).
    auto finish = [this, tok](std::set<int> tools) {
        tok.defer("DetailView::headless_scan_finish", [this, tools = std::move(tools)]() mutable {
            headless_tools_used_ = std::move(tools);
            headless_scan_done_ = true;
            spdlog::debug("[DetailView] Headless tools_used scan complete: {} tools",
                          headless_tools_used_->size());

            // Render the per-tool color swatches from the REAL used-tool
            // set recovered by the headless scan. On 2D-only platforms
            // (Snapmaker U1, AD5M) the gcode viewer never parses, so
            // try_extract_gcode_colors() — the viewer-parse owner of this
            // render — never fires and the detail panel would otherwise
            // show no color info at all (regression 22d37fd47). Mirror its
            // visibility decision and renderer here, sourcing the tool set
            // from tools_used_effective() so the swatches reflect the
            // precise used tools (e.g. {0,2}), not an over-counted palette.
            //
            // Guard on !is_gcode_loaded(): when the viewer DID parse (full
            // platforms) it already owns the render — don't double-fire.
            if (!is_gcode_loaded()) {
                const bool mapping_visible = filament_mapping_card_.should_show();
                const auto tools_used = tools_used_effective();
                const bool swatches_visible =
                    !mapping_visible && swatches_card_visible_for(tools_used.size());
                lv_subject_set_int(&color_swatches_visible_, swatches_visible ? 1 : 0);
                if (swatches_visible) {
                    update_color_swatches(tools_used, current_filament_colors_);
                }
                // Refresh the mapping card's DISPLAY from the headless
                // colors. Still required for editable-card backends that
                // use the headless path (e.g. CFS on a 2D-only platform):
                // the card widget renders its own swatches/rows from
                // tool_info_, so it must be fed here just as the
                // viewer-parse path does (try_extract_gcode_colors).
                //
                // NOTE: this is now redundant for preflight/remap LOGIC —
                // recompute_preflight() and open_remap_modal() source
                // per-tool info from current_filament_colors_/materials
                // directly (get_used_tool_info()), not the card instance —
                // but it remains correct and necessary for card display.
                filament_mapping_card_.update(current_filament_colors_,
                                              current_filament_materials_);
            }

            // Refresh pre-flight using the headless set (no-op on full
            // platforms where the viewer parse already populated it).
            recompute_preflight();

            // Restrict the mapping card to the tools this file actually uses,
            // sourced from the headless scan result. Empty/unknown ⇒ show all.
            filament_mapping_card_.set_used_tools(tools_used_effective());

            // Release any deferred print attempt waiting on readiness.
            fire_on_preflight_ready();
        });
    };

    // Stream the whole file to disk (memory-safe), then scan it line-by-line off
    // the main thread. The scan retains ONLY the int set — no geometry — so it is
    // safe on constrained devices. download_file_to_path runs on the HTTP slow
    // lane internally; the success/error callbacks run on the HTTP thread, so we
    // do the (bg-only, no `this`) scan there and marshal the result via tok.defer.
    api_->transfers().download_file_to_path(
        "gcodes", file_path, scan_path,
        [scan_path, finish](const std::string& path) mutable {
            // HTTP thread: parse to a LOCAL set (no `this` access), then delete the
            // temp file. The scanner streams from disk and never holds the whole
            // file in memory.
            std::set<int> tools = helix::gcode::scan_tools_used_from_file(path);
            std::remove(scan_path.c_str());
            finish(std::move(tools));
        },
        [scan_path, finish](const MoonrakerError& err) mutable {
            // HTTP thread: download failed — degrade gracefully with an empty set.
            spdlog::warn("[DetailView] Headless tools scan download failed: {} — proceeding "
                         "without tools_used",
                         err.message);
            std::remove(scan_path.c_str());
            finish({});
        });
}

bool PrintSelectDetailView::swatches_card_visible_for(size_t tool_count) const {
    // Multi-tool printers: any tool referenced is enough (lane identity matters).
    // Single-extruder: 2+ tools required (manual-swap multi-color files).
    const int ams_slots = lv_subject_get_int(AmsState::instance().get_slot_count_subject());
    const bool is_multi_tool_printer =
        helix::ToolState::instance().is_multi_tool() || ams_slots > 1;
    return is_multi_tool_printer ? tool_count > 0 : tool_count > 1;
}

void PrintSelectDetailView::load_gcode_for_preview() {
    // Skip if no viewer widget
    if (!gcode_viewer_) {
        spdlog::debug("[DetailView] No gcode_viewer_ widget - skipping G-code preview");
        return;
    }

    // Skip if no API available
    if (!api_) {
        spdlog::debug("[DetailView] No API available - skipping G-code preview");
        return;
    }

    // Skip if no filename
    if (current_filename_.empty()) {
        spdlog::debug("[DetailView] No filename - skipping G-code preview");
        return;
    }

    // Clear previous model so stale frames don't flash when viewer becomes visible
    ui_gcode_viewer_clear(gcode_viewer_);

    // Reset first-frame flag: the thumbnail stays on top until the viewer
    // renders, then the first-frame callback flips this to reveal it.
    lv_subject_set_int(&detail_viewer_first_frame_, 0);

    // Register one-shot callback: fires after the viewer's first complete
    // render, at which point we hide the thumbnail (revealing the viewer).
    ui_gcode_viewer_set_first_frame_callback(
        gcode_viewer_,
        [](lv_obj_t* /*viewer*/, void* user_data, bool /*success*/) {
            auto* self = static_cast<PrintSelectDetailView*>(user_data);
            spdlog::debug("[DetailView] First frame rendered — revealing viewer");
            lv_subject_set_int(&self->detail_viewer_first_frame_, 1);
        },
        this);

    // Show loading spinner over thumbnail
    lv_subject_set_int(&detail_gcode_loading_, 1);

    // Check "Thumbnail Only" render mode - skip all gcode downloading/parsing.
    // This is the ONLY user-forced skip: past here we render whatever mode the
    // viewer is in (3D on GLES devices, 2D-layer otherwise), just like the
    // print-status panel. 2D-mode devices used to fall back to the thumbnail
    // here; now they get the real toolpath preview — with the effective
    // lane-matched colors (apply_preview_colors) — so the browser shows the
    // colors the print will actually use. Oversized files still degrade to the
    // thumbnail below via is_gcode_2d_streaming_safe().
    if (DisplaySettingsManager::instance().get_gcode_render_mode() == 3) {
        spdlog::info("[DetailView] G-code render mode is Thumbnail Only - skipping G-code load");
        lv_subject_set_int(&detail_gcode_loading_, 0);
        show_gcode_viewer(false);
        return;
    }

    // Generate temp file path with caching
    std::string cache_dir = get_helix_cache_dir("gcode_temp");
    if (cache_dir.empty()) {
        spdlog::warn("[DetailView] No writable cache directory - skipping G-code preview");
        lv_subject_set_int(&detail_gcode_loading_, 0);
        show_gcode_viewer(false);
        return;
    }
    std::string temp_path = cache_dir + "/detail_preview_" +
                            std::to_string(std::hash<std::string>{}(current_filename_)) + ".gcode";

    // Check if file already exists and is non-empty (cached from previous session)
    std::ifstream cached_file(temp_path, std::ios::binary | std::ios::ate);
    if (cached_file && cached_file.tellg() > 0) {
        size_t cached_size = static_cast<size_t>(cached_file.tellg());
        cached_file.close();

        if (helix::is_gcode_2d_streaming_safe(cached_size)) {
            spdlog::info("[DetailView] Using cached G-code file ({} bytes): {}", cached_size,
                         temp_path);
            temp_gcode_path_ = temp_path;

            // Set up load callback and load the file
            ui_gcode_viewer_set_load_callback(
                gcode_viewer_,
                [](lv_obj_t* viewer, void* user_data, bool success) {
                    auto* self = static_cast<PrintSelectDetailView*>(user_data);
                    if (!success) {
                        spdlog::warn("[DetailView] G-code load failed from cache");
                        self->show_gcode_viewer(false);
                        return;
                    }
                    self->gcode_loaded_ = true;

                    // Show all layers, no ghost (preview = full model)
                    ui_gcode_viewer_set_print_progress(viewer, -1);

                    // Apply preview colors respecting the sliced/actual toggle
                    // (default actual: AMS/slicer base then mapped overrides).
                    self->apply_preview_colors();

                    // Extract colors from parsed gcode when metadata lacked them.
                    // This also computes preflight_result_ — it MUST run before
                    // fire_on_loaded() so any deferred print-attempt sees fresh checks.
                    self->try_extract_gcode_colors(viewer);

                    // Parse + pre-flight are now complete: release any deferred
                    // run_when_loaded() callback (e.g. a print tapped pre-parse).
                    self->fire_on_loaded();
                    // The viewer parse also satisfies pre-flight readiness on full
                    // platforms — release any run_when_preflight_ready() attempt.
                    self->fire_on_preflight_ready();

                    // Unpause, show, then reset camera (must be visible for layout)
                    ui_gcode_viewer_set_paused(viewer, false);
                    self->show_gcode_viewer(true);
                    lv_obj_update_layout(viewer);
                    ui_gcode_viewer_reset_camera(viewer);

                    spdlog::debug("[DetailView] G-code preview loaded from cache");
                },
                this);
            ui_gcode_viewer_load_file(gcode_viewer_, temp_path.c_str());
            return;
        } else {
            spdlog::debug("[DetailView] Cached file too large for streaming, removing");
            std::remove(temp_path.c_str());
        }
    }

    // Build full relative path for metadata lookup and download
    std::string file_path =
        current_path_.empty() ? current_filename_ : current_path_ + "/" + current_filename_;
    std::string metadata_filename = file_path;

    auto tok = lifetime_.token();

    api_->files().get_file_metadata(
        metadata_filename,
        [this, tok, temp_path, file_path](const FileMetadata& metadata) {
            // L081 Mechanism C: marshal member writes + LVGL/show_gcode_viewer
            // to main thread before touching `this`.
            tok.defer("DetailView::metadata_apply", [this, tok, metadata, temp_path, file_path]() {
                // Cache for PrintStartController's pre-print checks (e.g., filament weight)
                cached_file_metadata_ = metadata;

                // Check if file is safe to render given available RAM
                if (!helix::is_gcode_2d_streaming_safe(metadata.size)) {
                    auto mem = helix::get_system_memory_info();
                    spdlog::warn("[DetailView] G-code too large for streaming: file={} bytes, "
                                 "available RAM={}MB - using thumbnail",
                                 metadata.size, mem.available_mb());
                    show_gcode_viewer(false);
                    return;
                }

                spdlog::debug("[DetailView] G-code size {} bytes - safe to render, downloading...",
                              metadata.size);

                // Clean up previous temp file if different
                if (!temp_gcode_path_.empty() && temp_gcode_path_ != temp_path) {
                    std::remove(temp_gcode_path_.c_str());
                    temp_gcode_path_.clear();
                }

                // Stream download to disk
                api_->transfers().download_file_to_path(
                    "gcodes", file_path, temp_path,
                    [this, tok, temp_path](const std::string& path) {
                        // Runs on HTTP thread — no bg-thread tok.expired() check (L081 Mechanism
                        // C). tok.defer() marshals the body to the main thread and re-checks the
                        // generation there, which is what gates the member access below.
                        tok.defer("DetailView::gcode_downloaded", [this, path]() {
                            temp_gcode_path_ = path;

                            spdlog::debug("[DetailView] G-code downloaded, loading into viewer: {}",
                                          path);

                            // Set up load callback
                            ui_gcode_viewer_set_load_callback(
                                gcode_viewer_,
                                [](lv_obj_t* viewer, void* user_data, bool success) {
                                    auto* self = static_cast<PrintSelectDetailView*>(user_data);
                                    if (!success) {
                                        spdlog::warn(
                                            "[DetailView] G-code load failed after download");
                                        self->show_gcode_viewer(false);
                                        return;
                                    }
                                    self->gcode_loaded_ = true;

                                    // Show all layers, no ghost (preview = full model)
                                    ui_gcode_viewer_set_print_progress(viewer, -1);

                                    // Apply preview colors respecting the sliced/actual toggle.
                                    self->apply_preview_colors();

                                    // Extract colors from parsed gcode when metadata lacked them.
                                    // Also computes preflight_result_ — MUST run before
                                    // fire_on_loaded() so a deferred print sees fresh checks.
                                    self->try_extract_gcode_colors(viewer);

                                    // Parse + pre-flight complete: release any deferred
                                    // run_when_loaded() callback (e.g. a pre-parse print tap).
                                    self->fire_on_loaded();
                                    // Viewer parse also satisfies pre-flight readiness
                                    // on full platforms.
                                    self->fire_on_preflight_ready();

                                    // Unpause, show, then reset camera (must be visible for layout)
                                    ui_gcode_viewer_set_paused(viewer, false);
                                    self->show_gcode_viewer(true);
                                    lv_obj_update_layout(viewer);
                                    ui_gcode_viewer_reset_camera(viewer);

                                    spdlog::debug(
                                        "[DetailView] G-code preview loaded successfully");
                                },
                                this);

                            // Load into viewer
                            ui_gcode_viewer_load_file(gcode_viewer_, path.c_str());
                        });
                    },
                    [this, tok](const MoonrakerError& err) {
                        // Runs on HTTP thread — no bg-thread tok.expired() check (L081 Mechanism
                        // C); tok.defer() re-checks on the main thread instead.
                        spdlog::warn("[DetailView] Failed to download G-code: {}", err.message);
                        tok.defer("DetailView::gcode_download_error",
                                  [this]() { show_gcode_viewer(false); });
                    });
            });
        },
        [this, tok](const MoonrakerError& err) {
            // L081 Mechanism C: marshal LVGL show_gcode_viewer to main thread.
            tok.defer("DetailView::metadata_error", [this, err]() {
                spdlog::debug("[DetailView] Failed to get G-code metadata: {} - skipping preview",
                              err.message);
                show_gcode_viewer(false);
            });
        },
        true // silent
    );
}

// ============================================================================
// Pre-print Estimate Label Update
// ============================================================================

static void update_prep_time_label() {
    if (!s_detail_view_instance || !s_detail_view_instance->get_prep_manager()) {
        return;
    }
    auto* mgr = s_detail_view_instance->get_prep_manager();
    mgr->recalculate_estimate();

    int estimate_s = lv_subject_get_int(mgr->get_preprint_estimate_subject());

    if (estimate_s <= 0) {
        lv_subject_copy_string(s_detail_view_instance->get_prep_time_estimate_subject(), "");
        return;
    }

    // Round: >120s to nearest 30s, <=120s to nearest 10s
    int rounded = estimate_s > 120 ? ((estimate_s + 15) / 30) * 30 : ((estimate_s + 5) / 10) * 10;
    int mins = rounded / 60;
    int secs = rounded % 60;
    char buf[48];
    if (mins > 0 && secs > 0) {
        snprintf(buf, sizeof(buf), "~%d:%02d prep time", mins, secs);
    } else if (mins > 0) {
        snprintf(buf, sizeof(buf), "~%d min prep time", mins);
    } else {
        snprintf(buf, sizeof(buf), "~%d sec prep time", secs);
    }
    lv_subject_copy_string(s_detail_view_instance->get_prep_time_estimate_subject(), buf);
}

// ============================================================================
// Dynamic option-row population
// ============================================================================
//
// `pre_print_options_container_` is populated from the active printer's
// `PrePrintOptionSet`. Each option becomes a row with a label + switch in a
// flat list (categories are sort keys only — no subheaders). This replaces
// the previous hardcoded XML rows + per-option static callbacks.
//
// The renderer owns one heap-allocated lv_subject_t per option (the toggle
// state). We register a state provider on `prep_manager_` so that
// `collect_macro_skip_params()` and friends can read these subjects without
// needing to know about their LVGL pointers — they query by id.
//
// Per-row visibility: the renderer's `VisibilitySubjectLookup` callback is
// invoked for each option; returning nullptr leaves the row unconditionally
// visible. Today only the plugin-gated predicate returns a non-null subject
// (the helix_plugin_installed tri-state); macro-gated options are filtered
// out of the set BEFORE populate() is called (see
// filter_macro_gated_options), so the lookup never sees them.

void PrintSelectDetailView::populate_option_rows() {
    if (!pre_print_options_container_) {
        return;
    }

    if (!printer_state_) {
        return;
    }

    const auto& option_set = printer_state_->get_pre_print_option_set();

    // Skip rebuild only when rows are already populated AND the active
    // printer hasn't changed since they were built. Mid-session printer-type
    // changes (e.g. multi-printer setups) need a repopulate so the rows
    // reflect the new option set.
    //
    // The rebuild path is safe: `populate()` calls `clear()` (which deinits
    // every option subject — uninstalling observers from their row widgets)
    // BEFORE `safe_clean_children` deletes the widgets themselves. That
    // ordering is what the renderer's class doc spells out as "case 1" of
    // the lifetime contract — observers are uninstalled while widgets are
    // still alive, so the deferred widget-delete tick has nothing to do for
    // them. Repopulating mid-session is therefore not the race that this
    // early-return originally guarded against.
    const std::string& current_type = printer_state_->get_printer_type();
    if (option_rows_renderer_.row_count() > 0 && current_type == last_rendered_printer_type_) {
        spdlog::trace("[DetailView] Skipping option-row rebuild (already populated for '{}')",
                      current_type);
        return;
    }
    last_rendered_printer_type_ = current_type;

    // Honor `PrePrintOption::requires_macro`: hide options whose required
    // macro isn't registered with Klipper. Their toggles would be inert —
    // collect_pre_start_gcode_lines() already drops their gcode at print
    // start (see ui_print_preparation_manager.cpp), so showing the row only
    // confuses users. e.g. K2 Plus "AI Detect" without LOAD_AI_RUN installed.
    //
    // MacroParamCache is populated once during connection discovery and
    // lives until disconnect; this panel rebuilds on printer-type change, so
    // a populate-time filter is correct (no reactive subject needed, unlike
    // the plugin-gated visibility_lookup below).
    PrePrintOptionSet rendered = filter_macro_gated_options(option_set);
    if (rendered.options.size() != option_set.options.size()) {
        spdlog::debug("[DetailView] Filtered {} macro-gated option row(s) (printer='{}')",
                      option_set.options.size() - rendered.options.size(), current_type);
    }

    // Plugin-gated visibility: HIDE a toggle only when DISABLING it would
    // require the HelixPrint plugin (see
    // PrintPreparationManager::disabling_option_requires_plugin). For those
    // options we bind the row to the helix_plugin_installed tri-state subject;
    // the renderer hides the row when it reads 0 (plugin confirmed absent) and
    // keeps it visible at -1 (still checking, startup window) and 1 (present).
    //
    // CAUTION — do NOT hide options the printer handles natively without the
    // plugin. K2 Plus bed_mesh is a MacroParam whose START_PRINT/PRINT_PREPARED
    // takes the skip directly (setup_gcode present), so the predicate returns
    // false and it stays visible. Hiding it was a shipped regression; the
    // predicate now enforces the distinction structurally.
    auto visibility_lookup = [this](const std::string& id) -> lv_subject_t* {
        if (!prep_manager_ || !printer_state_) {
            return nullptr;
        }
        const PrePrintOption* opt = printer_state_->get_pre_print_option_set().find(id);
        if (opt && prep_manager_->disabling_option_requires_plugin(*opt)) {
            return printer_state_->get_helix_plugin_installed_subject();
        }
        return nullptr; // Not plugin-dependent: always visible for declared options.
    };

    option_rows_renderer_.populate(pre_print_options_container_, rendered, visibility_lookup,
                                   [](const std::string& id, int new_state) {
                                       spdlog::debug("[DetailView] Option '{}' toggled: {}", id,
                                                     new_state);
                                       update_prep_time_label();
                                   });

    // Wire up the option-state provider on the prep manager so that
    // collect_macro_skip_params() reads from these dynamic subjects. -1 means
    // "not bound" — manager falls back to its legacy subject path or the
    // option's default.
    if (prep_manager_) {
        prep_manager_->set_option_state_provider([this](const std::string& id) -> int {
            // Only return 0/1 for ids the renderer actually has rows for.
            // Otherwise defer to the manager's fallback chain.
            auto ids = option_rows_renderer_.rendered_ids();
            for (const auto& rid : ids) {
                if (rid == id) {
                    return option_rows_renderer_.get_state(id, 0);
                }
            }
            return -1;
        });
    }
}

} // namespace helix::ui
