// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_context_menu.h"

#include "ui_button.h"
#include "ui_callback_helpers.h"
#include "ui_error_reporting.h"
#include "ui_toast_manager.h"

#include "ams_backend.h"
#include "ams_state.h"
#include "ams_types.h"
#include "app_globals.h"
#include "filament_database.h"
#include "filament_op_slot_resolver.h"
#include "printer_state.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

namespace helix::ui {

// Static member initialization
bool AmsContextMenu::callbacks_registered_ = false;
AmsContextMenu* AmsContextMenu::s_active_instance_ = nullptr;

// ============================================================================
// Construction / Destruction
// ============================================================================

AmsContextMenu::AmsContextMenu() {
    // Initialize subjects for button enabled states
    lv_subject_init_int(&slot_is_loaded_subject_, 0);
    lv_xml_register_subject(nullptr, "ams_slot_is_loaded", &slot_is_loaded_subject_);

    lv_subject_init_int(&slot_can_load_subject_, 1);
    lv_xml_register_subject(nullptr, "ams_slot_can_load", &slot_can_load_subject_);

    lv_subject_init_int(&slot_source_external_subject_, 0);
    lv_xml_register_subject(nullptr, "ams_slot_source_external", &slot_source_external_subject_);

    subject_initialized_ = true;
    spdlog::debug("[AmsContextMenu] Constructed");
}

AmsContextMenu::~AmsContextMenu() {
    // Clear active instance before base destructor calls hide()
    if (s_active_instance_ == this) {
        s_active_instance_ = nullptr;
    }

    // Clean up subjects
    if (subject_initialized_ && lv_is_initialized()) {
        lv_subject_deinit(&slot_is_loaded_subject_);
        lv_subject_deinit(&slot_can_load_subject_);
        lv_subject_deinit(&slot_source_external_subject_);
        subject_initialized_ = false;
    }
    spdlog::trace("[AmsContextMenu] Destroyed");
}

AmsContextMenu::AmsContextMenu(AmsContextMenu&& other) noexcept
    : ContextMenu(std::move(other)), action_callback_(std::move(other.action_callback_)),
      subject_initialized_(other.subject_initialized_), backend_(other.backend_),
      total_slots_(other.total_slots_), tool_dropdown_(other.tool_dropdown_),
      backup_dropdown_(other.backup_dropdown_), pending_is_loaded_(other.pending_is_loaded_) {
    // Transfer subject ownership
    if (other.subject_initialized_) {
        slot_is_loaded_subject_ = other.slot_is_loaded_subject_;
        slot_can_load_subject_ = other.slot_can_load_subject_;
        slot_source_external_subject_ = other.slot_source_external_subject_;
    }
    // Update static instance
    if (s_active_instance_ == &other) {
        s_active_instance_ = this;
    }
    other.backend_ = nullptr;
    other.total_slots_ = 0;
    other.tool_dropdown_ = nullptr;
    other.backup_dropdown_ = nullptr;
    other.subject_initialized_ = false;
}

AmsContextMenu& AmsContextMenu::operator=(AmsContextMenu&& other) noexcept {
    if (this != &other) {
        // Clear our active instance before base hide()
        if (s_active_instance_ == this) {
            s_active_instance_ = nullptr;
        }

        // Let base class handle its state
        ContextMenu::operator=(std::move(other));

        action_callback_ = std::move(other.action_callback_);
        backend_ = other.backend_;
        total_slots_ = other.total_slots_;
        tool_dropdown_ = other.tool_dropdown_;
        backup_dropdown_ = other.backup_dropdown_;
        pending_is_loaded_ = other.pending_is_loaded_;

        // Transfer subject ownership
        if (other.subject_initialized_) {
            slot_is_loaded_subject_ = other.slot_is_loaded_subject_;
            slot_can_load_subject_ = other.slot_can_load_subject_;
            slot_source_external_subject_ = other.slot_source_external_subject_;
        }
        subject_initialized_ = other.subject_initialized_;

        if (s_active_instance_ == &other) {
            s_active_instance_ = this;
        }

        other.backend_ = nullptr;
        other.total_slots_ = 0;
        other.tool_dropdown_ = nullptr;
        other.backup_dropdown_ = nullptr;
        other.subject_initialized_ = false;
    }
    return *this;
}

// ============================================================================
// Public API
// ============================================================================

void AmsContextMenu::set_action_callback(ActionCallback callback) {
    action_callback_ = std::move(callback);
}

bool AmsContextMenu::show_near_widget(lv_obj_t* parent, int slot_index, lv_obj_t* near_widget,
                                      bool is_loaded, AmsBackend* backend) {
    // Register callbacks once (idempotent)
    register_callbacks();

    // Store AMS-specific state BEFORE base class calls on_created
    backend_ = backend;
    pending_is_loaded_ = is_loaded;
    external_spool_mode_ = false;

    // Get total slots from backend
    if (backend_) {
        total_slots_ = backend_->get_system_info().total_slots;
    } else {
        total_slots_ = 0;
    }

    // Set as active instance for static callbacks
    s_active_instance_ = this;

    // Base class handles: XML creation, on_created callback, positioning
    bool result = ContextMenu::show_near_widget(parent, slot_index, near_widget);
    if (!result) {
        s_active_instance_ = nullptr;
    }

    spdlog::debug("[AmsContextMenu] Shown for slot {}", slot_index);
    return result;
}

bool AmsContextMenu::show_for_external_spool(lv_obj_t* parent, lv_obj_t* anchor_widget) {
    // Register callbacks once (idempotent)
    register_callbacks();

    // Configure for external spool mode (no backend operations)
    backend_ = nullptr;
    pending_is_loaded_ = false;
    total_slots_ = 0;
    external_spool_mode_ = true;

    // Set as active instance for static callbacks
    s_active_instance_ = this;

    // Base class handles: XML creation, on_created callback, positioning
    bool result = ContextMenu::show_near_widget(parent, -2, anchor_widget);
    if (!result) {
        s_active_instance_ = nullptr;
        external_spool_mode_ = false;
    }

    spdlog::debug("[AmsContextMenu] Shown for external spool");
    return result;
}

// ============================================================================
// ContextMenu override
// ============================================================================

void AmsContextMenu::handle_open_source() {
    spdlog::info("[AmsContextMenu] Open source unit {} for slot {}", source_owner_unit_,
                 get_item_index());
    dispatch_ams_action(MenuAction::OPEN_SOURCE_UNIT);
}

void AmsContextMenu::on_created(lv_obj_t* menu_obj) {
    int slot_index = get_item_index();

    // Does another unit own this slot's filament identity? On multiACE an
    // ACE-fed U1 head's material/colour/spool are the ACE's to state, and
    // editing them here would write print_task_config only for the ACE to
    // overwrite it on its next report. The edit actions bind to this subject and
    // hide themselves; "Open in <unit>" binds to its inverse. Load and Unload
    // are untouched — they act on the head, which is still the U1's job.
    source_owner_unit_ = -1;
    if (!external_spool_mode_ && backend_) {
        if (auto owner = backend_->slot_identity_owner_unit(slot_index)) {
            source_owner_unit_ = *owner;
        }
    }
    lv_subject_set_int(&slot_source_external_subject_, source_owner_unit_ >= 0 ? 1 : 0);
    spdlog::debug("[AmsContextMenu] slot {} identity owner unit = {} (backend={})", slot_index,
                  source_owner_unit_, backend_ ? "yes" : "null");

    // External spool mode: hide backend-related buttons, show only EDIT/CLEAR
    if (external_spool_mode_) {
        // Hide Load/Unload buttons (not applicable to external spool)
        lv_obj_t* btn_load = lv_obj_find_by_name(menu_obj, "btn_load");
        if (btn_load)
            lv_obj_add_flag(btn_load, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t* btn_unload = lv_obj_find_by_name(menu_obj, "btn_unload");
        if (btn_unload)
            lv_obj_add_flag(btn_unload, LV_OBJ_FLAG_HIDDEN);

        // Disable subject-driven states so hidden buttons stay hidden
        lv_subject_set_int(&slot_is_loaded_subject_, 0);
        lv_subject_set_int(&slot_can_load_subject_, 0);

        // Set header to "External Spool"
        lv_obj_t* slot_header = lv_obj_find_by_name(menu_obj, "slot_header");
        if (slot_header) {
            lv_label_set_text(slot_header, lv_tr("External Spool"));
        }

        // Show Clear Spool button when external spool has an assignment.
        // btn_edit ("Spool Info") is always shown so users can reopen the edit modal.
        auto ext_info = AmsState::instance().get_external_spool_info();
        bool has_assignment =
            ext_info.has_value() && (ext_info->spoolman_id > 0 || !ext_info->material.empty());

        lv_obj_t* btn_clear = lv_obj_find_by_name(menu_obj, "btn_clear_spool");
        if (btn_clear && has_assignment) {
            lv_obj_clear_flag(btn_clear, LV_OBJ_FLAG_HIDDEN);
        }

        // Show "Select Spool" and "Scan QR Code" if Spoolman is available
        auto* spoolman_subj = lv_xml_get_subject(nullptr, "printer_has_spoolman");
        bool has_spoolman = spoolman_subj && lv_subject_get_int(spoolman_subj) == 1;

        lv_obj_t* btn_spoolman = lv_obj_find_by_name(menu_obj, "btn_spoolman");
        if (btn_spoolman && has_spoolman) {
            lv_obj_clear_flag(btn_spoolman, LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_t* btn_scan_qr = lv_obj_find_by_name(menu_obj, "btn_scan_qr");
        if (btn_scan_qr && has_spoolman) {
            lv_obj_clear_flag(btn_scan_qr, LV_OBJ_FLAG_HIDDEN);
        }

        // No dropdowns for external spool
        return;
    }

    // Whether a print blocks these ops is print_blocks_filament_op(), the mirror
    // of AmsSubscriptionBackend::refuse_if_printing(): PRINTING always, PAUSED
    // only on a backend whose filament macro homes itself (AD5X IFS). Reading
    // the raw print_active subject here — which is 1 for both — would keep the
    // menu greyed through the runout pause that is the whole recovery workflow.
    const auto job_state = static_cast<helix::PrintJobState>(
        lv_subject_get_int(get_printer_state().get_print_state_enum_subject()));
    const bool print_blocks_op = helix::ui::print_blocks_filament_op(
        job_state == helix::PrintJobState::PRINTING, job_state == helix::PrintJobState::PAUSED,
        backend_ && backend_->filament_ops_self_home());

    // Check if system is busy (operation in progress). AmsSystemInfo::is_busy()
    // is the same predicate AmsSubscriptionBackend::check_preconditions() uses to
    // refuse — this used to open-code it, which is how the filament panel came to
    // have no busy term at all.
    bool system_busy = false;
    if (backend_) {
        AmsSystemInfo info = backend_->get_system_info();
        system_busy = info.is_busy();
        if (system_busy) {
            spdlog::debug("[AmsContextMenu] System busy ({}), disabling Load/Unload",
                          ams_action_to_string(info.action));
        }
    }

    // Get slot info for filament presence check. Tri-state: slot_presence()
    // reports SlotStatus::UNKNOWN as "unanswerable" rather than "empty", so a
    // backend that publishes no per-lane presence does not grey Load.
    std::optional<bool> slot_has_filament;
    // LIVE load state (Task 3): firmware seated+loaded OR filament at this slot's
    // toolhead sensor — drives Load/Unload availability instead of static RFID
    // presence so the menu matches real load state the moment a sensor flips.
    bool slot_is_loaded_live = false;
    if (backend_) {
        SlotInfo slot_info = backend_->get_slot_info(slot_index);
        slot_has_filament = helix::ui::slot_presence(slot_info);
        slot_is_loaded_live = backend_->slot_is_actively_loaded(slot_index) ||
                              backend_->slot_has_filament_at_toolhead(slot_index);
    }

    // Treat the slot as loaded if EITHER the caller's snapshot (can_unload_from_
    // toolhead, computed at open) OR the live per-slot accessors say so. The OR
    // keeps Unload available for the firmware's active slot even after a runout
    // clears the head sensor (#995), while live signals enable real-time accuracy.
    const bool is_loaded = pending_is_loaded_ || slot_is_loaded_live;

    // Whether this slot's unload action is a heated toolhead unload (true) or a
    // cold per-lane eject (false). Defaults to is_loaded for most backends; AD5X
    // IFS refines it with seated-channel authority so a NON-seated lane reads
    // "Eject" even when the firmware dropped its active-slot pointer and
    // is_loaded was broadened by the recovery clause (raza616, 5HR3HHS6). This
    // drives both the button label and the dispatched action (handle_unload).
    const bool toolhead_unload =
        backend_ ? backend_->slot_unloads_to_toolhead(slot_index, is_loaded) : is_loaded;

    // Select the Unload button's operation, most specific first. Each mode has a
    // distinct label and a distinct dispatch; see UnloadMode and decide_unload_mode().
    //
    // The unload-mode decision keeps the strict reading of presence — "is there
    // something to eject" has no useful third answer, and an unknown lane must
    // not grow an Eject button out of nowhere. Only the Load gate treats UNKNOWN
    // as unanswerable.
    const bool slot_filament_present = slot_has_filament.value_or(false);
    const bool slot_empty = !slot_filament_present;
    const bool supports_eject = backend_ && backend_->supports_lane_eject();
    const bool can_recover = backend_ && backend_->can_recover_lane_position(slot_index);
    const bool recovery_attributed = backend_ && backend_->lane_recovery_is_attributed();
    const bool supports_force_eject = backend_ && backend_->supports_force_eject();

    unload_mode_ =
        decide_unload_mode(toolhead_unload, can_recover, recovery_attributed, supports_eject,
                           slot_filament_present, supports_force_eject, slot_empty);

    const bool unload_enabled =
        decide_unload_enabled(system_busy, unload_mode_, print_blocks_op,
                              backend_ && backend_->cold_lane_ops_refused_during_print());
    lv_subject_set_int(&slot_is_loaded_subject_, unload_enabled ? 1 : 0);

    lv_obj_t* btn_unload = lv_obj_find_by_name(menu_obj, "btn_unload");
    if (btn_unload) {
        switch (unload_mode_) {
        case UnloadMode::RecoverPosition:
        case UnloadMode::ForceEject:
            ui_button_set_text(btn_unload, lv_tr("Recover"));
            ui_button_set_icon(btn_unload, "eject");
            break;
        case UnloadMode::Eject:
            ui_button_set_text(btn_unload, lv_tr("Eject"));
            ui_button_set_icon(btn_unload, "eject");
            break;
        case UnloadMode::Unload:
        case UnloadMode::Unavailable:
            break; // XML defaults: "Unload"
        }
    }

    // QIDI Box: a lane with ejectable filament but [force_move] enable_force_move
    // off means eject is unavailable (supports_lane_eject() is false). Surface a
    // one-line hint pointing at the config instead of silently omitting the
    // action — for QIDI, !supports_eject here can only mean force_move is off
    // (the box always supports eject otherwise). See #1041.
    if (backend_ && backend_->get_type() == AmsType::QIDI_BOX && !supports_eject &&
        !pending_is_loaded_ && slot_filament_present) {
        lv_obj_t* hint = lv_obj_find_by_name(menu_obj, "eject_force_move_hint");
        if (hint) {
            lv_obj_remove_flag(hint, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Determine if slot has filament for Load button state.
    // Gate on ACTUAL toolhead-loaded state (toolhead_unload), NOT the broadened
    // is_loaded recovery signal. is_loaded folds in can_unload_from_toolhead, which
    // reads true for the firmware's seated channel regardless of whether filament
    // actually reached the head. A cold-lane-eject lane (filament parked in the
    // lane but NOT at the toolhead) is a valid Load target, so it must stay
    // enabled. For non-AD5X backends slot_unloads_to_toolhead() returns the hint
    // unchanged, so !toolhead_unload == !is_loaded and behavior is unaffected.
    // Disable Load if: system busy, slot empty, OR filament is already at the head.
    bool can_load = decide_can_load(system_busy, toolhead_unload, slot_has_filament,
                                    print_blocks_op, source_owner_unit_ >= 0);
    lv_subject_set_int(&slot_can_load_subject_, can_load ? 1 : 0);
    if (!can_load) {
        spdlog::debug("[AmsContextMenu] Load disabled for slot {}: busy={}, loaded={} "
                      "(live={}), has_filament={}, print_blocks_op={}",
                      slot_index, system_busy, is_loaded, slot_is_loaded_live,
                      slot_has_filament ? (*slot_has_filament ? "yes" : "no") : "unknown",
                      print_blocks_op);
    }

    // Show Select Gate button if backend supports it (e.g. Happy Hare)
    if (backend_ && backend_->supports_gate_select()) {
        lv_obj_t* btn_gate_select = lv_obj_find_by_name(menu_obj, "btn_gate_select");
        if (btn_gate_select) {
            lv_obj_remove_flag(btn_gate_select, LV_OBJ_FLAG_HIDDEN);
            if (system_busy) {
                lv_obj_add_state(btn_gate_select, LV_STATE_DISABLED);
            }
        }
    }

    // Show Check Gate button if backend supports it (e.g. Happy Hare)
    if (backend_ && backend_->supports_gate_check()) {
        lv_obj_t* btn_gate_check = lv_obj_find_by_name(menu_obj, "btn_gate_check");
        if (btn_gate_check) {
            lv_obj_remove_flag(btn_gate_check, LV_OBJ_FLAG_HIDDEN);
            if (system_busy) {
                lv_obj_add_state(btn_gate_check, LV_STATE_DISABLED);
            }
        }
    }

    // Show Clear Spool whenever the slot carries an assignment, present or not
    // (should_show_clear_spool). It used to be gated on an EMPTY slot, so it
    // disappeared the moment a spool went in — exactly when stale metadata does
    // damage, since that is when it gets printed with and when an edit aims a
    // Spoolman write at the previous spool.
    // `source_external` gates all three spool-identity actions below. They are
    // shown imperatively from here, so the XML bind_flag that hides the rest
    // cannot reach them — a clear_flag after the binding simply wins. Naming the
    // condition once keeps the three in step.
    const bool source_external = source_owner_unit_ >= 0;
    if (backend_ && !source_external) {
        SlotInfo slot_info = backend_->get_slot_info(slot_index);
        lv_obj_t* btn_clear = lv_obj_find_by_name(menu_obj, "btn_clear_spool");
        if (btn_clear && should_show_clear_spool(slot_info)) {
            lv_obj_clear_flag(btn_clear, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Update the slot header text. Uses the SPOOL number the slot's badge shows,
    // not slot_index + 1 — those diverge once one slot views another's spool, and
    // a menu headed "Slot 5" opened from a badge reading "4" names nothing the
    // user can see. A single number rather than the badge's range: the menu acts
    // on this one slot.
    //
    // A position fed from ANOTHER unit is headed with that unit instead. It has
    // no slot number of its own — its badge is a range ("4-7"), because in head
    // mode every bay of the bound ACE feeds it — so "Slot 4" named a spool
    // position that does not exist. The unit's own display name ("ACE 2 Pro",
    // or "ACE 2" when several are attached) is the thing the user can point at.
    lv_obj_t* slot_header = lv_obj_find_by_name(menu_obj, "slot_header");
    if (slot_header) {
        char header_text[48];
        std::string owner_name;
        if (backend_ && source_owner_unit_ >= 0) {
            const AmsSystemInfo info = backend_->get_system_info();
            if (source_owner_unit_ < static_cast<int>(info.units.size())) {
                owner_name = info.units[static_cast<size_t>(source_owner_unit_)].display_name;
            }
        }
        if (!owner_name.empty()) {
            snprintf(header_text, sizeof(header_text), "%s", owner_name.c_str());
        } else {
            const int shown =
                backend_ ? backend_->spool_display_number(slot_index) : slot_index + 1;
            snprintf(header_text, sizeof(header_text), lv_tr("Slot %d"), shown);
        }
        lv_label_set_text(slot_header, header_text);
    }

    // Show "Select Spool" and "Scan QR Code" buttons if Spoolman is available
    auto* spoolman_subj = lv_xml_get_subject(nullptr, "printer_has_spoolman");
    bool has_spoolman = spoolman_subj && lv_subject_get_int(spoolman_subj) == 1;
    lv_obj_t* btn_spoolman = lv_obj_find_by_name(menu_obj, "btn_spoolman");
    if (btn_spoolman && has_spoolman && !source_external) {
        lv_obj_clear_flag(btn_spoolman, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t* btn_scan_qr = lv_obj_find_by_name(menu_obj, "btn_scan_qr");
    if (btn_scan_qr && has_spoolman && !source_external) {
        lv_obj_clear_flag(btn_scan_qr, LV_OBJ_FLAG_HIDDEN);
    }

    // Configure dropdowns based on backend capabilities
    configure_dropdowns();
}

// ============================================================================
// Event Handlers
// ============================================================================

void AmsContextMenu::dispatch_ams_action(MenuAction action) {
    int slot = get_item_index();
    ActionCallback callback_copy = action_callback_;

    if (s_active_instance_ == this) {
        s_active_instance_ = nullptr;
    }
    hide();

    if (callback_copy) {
        callback_copy(action, slot);
    }
}

void AmsContextMenu::handle_backdrop_clicked() {
    spdlog::debug("[AmsContextMenu] Backdrop clicked");
    dispatch_ams_action(MenuAction::CANCELLED);
}

void AmsContextMenu::handle_load() {
    spdlog::info("[AmsContextMenu] Load requested for slot {}", get_item_index());
    dispatch_ams_action(MenuAction::LOAD);
}

void AmsContextMenu::handle_unload() {
    switch (unload_mode_) {
    case UnloadMode::RecoverPosition:
        spdlog::info("[AmsContextMenu] Position recovery requested for slot {}", get_item_index());
        dispatch_ams_action(MenuAction::RECOVER_POSITION);
        break;
    case UnloadMode::Eject:
        spdlog::info("[AmsContextMenu] Eject requested for slot {}", get_item_index());
        dispatch_ams_action(MenuAction::EJECT);
        break;
    case UnloadMode::ForceEject:
        spdlog::info("[AmsContextMenu] Recover/force-eject requested for slot {}",
                     get_item_index());
        dispatch_ams_action(MenuAction::EJECT);
        break;
    case UnloadMode::Unload:
    case UnloadMode::Unavailable:
        spdlog::info("[AmsContextMenu] Unload requested for slot {}", get_item_index());
        dispatch_ams_action(MenuAction::UNLOAD);
        break;
    }
}

void AmsContextMenu::handle_gate_select() {
    spdlog::info("[AmsContextMenu] Select gate requested for slot {}", get_item_index());
    dispatch_ams_action(MenuAction::SELECT_GATE);
}

void AmsContextMenu::handle_gate_check() {
    spdlog::info("[AmsContextMenu] Check gate requested for slot {}", get_item_index());
    dispatch_ams_action(MenuAction::CHECK_GATE);
}

void AmsContextMenu::handle_edit() {
    spdlog::info("[AmsContextMenu] Edit requested for slot {}", get_item_index());
    dispatch_ams_action(MenuAction::EDIT);
}

bool AmsContextMenu::should_show_clear_spool(const SlotInfo& slot) {
    return slot.spoolman_id > 0 || !slot.material.empty();
}

AmsContextMenu::UnloadMode
AmsContextMenu::decide_unload_mode(bool toolhead_unload, bool can_recover, bool recovery_attributed,
                                   bool supports_eject, bool slot_has_filament,
                                   bool supports_force_eject, bool slot_empty) {
    if (toolhead_unload) {
        return UnloadMode::Unload;
    }
    if (can_recover && recovery_attributed) {
        // Filament stranded past the hub — a failed load or unload left it in the
        // bowden, where plain unload() cannot reach it. The backend named this
        // exact lane as the one needing recovery, so this is a confident
        // diagnosis: outrank Eject.
        return UnloadMode::RecoverPosition;
    }
    if (supports_eject && slot_has_filament) {
        return UnloadMode::Eject;
    }
    if (can_recover) {
        // Unattributed strand: some backends (AFC) share one hub sensor across
        // every lane on a unit, so can_recover can read true for every lane at
        // once with no way to say whose filament tripped it. Ranking this below
        // Eject means a seated lane (slot_has_filament above) keeps its Eject
        // button; this arm only catches lanes with nothing to eject, offering
        // Recover as a last resort rather than hiding it entirely.
        return UnloadMode::RecoverPosition;
    }
    if (supports_force_eject && slot_empty) {
        // Empty/runout lane: a cold presence-ignoring retract can clear a snapped
        // chunk the sensor cannot see (#996).
        return UnloadMode::ForceEject;
    }
    return UnloadMode::Unavailable;
}

bool AmsContextMenu::decide_can_load(bool system_busy, bool toolhead_unload,
                                     std::optional<bool> slot_has_filament, bool print_blocks_op,
                                     bool source_external) {
    // Checked before the shared gating, not folded into it: this is not a
    // "cannot right now" like busy or mid-print, it is "this position has no
    // such action". See the header.
    if (source_external) {
        return false;
    }
    helix::ui::OpButtonState state;
    state.system_busy = system_busy;
    state.print_blocks_op = print_blocks_op;
    state.slot_is_loaded = toolhead_unload;
    state.slot_has_filament = slot_has_filament;
    return !helix::ui::compute_op_button_gating(state).load_disabled;
}

bool AmsContextMenu::decide_unload_enabled(bool system_busy, UnloadMode mode, bool print_blocks_op,
                                           bool cold_ops_print_gated) {
    helix::ui::OpButtonState state;
    state.system_busy = system_busy;
    state.print_blocks_op = print_blocks_op;
    state.unload_available = (mode != UnloadMode::Unavailable);
    // Unload is the only mode that runs the heated toolhead path (and, on AD5X,
    // a self-homing firmware macro). Eject / RecoverPosition / ForceEject are
    // cold lane-side retracts that leave the toolhead where the print left it,
    // so they stay available for clearing a snapped strand mid-pause.
    //
    // Unless the firmware refuses them anyway: AFC's cmd_LANE_UNLOAD opens with
    // its own is_printing() check, so on that backend the exemption would offer a
    // button into a guaranteed refusal. cold_ops_print_gated is
    // AmsBackend::cold_lane_ops_refused_during_print() — see it for why this is
    // per-backend rather than a blanket rule.
    state.unload_is_cold_lane_op = (mode != UnloadMode::Unload) && !cold_ops_print_gated;
    return !helix::ui::compute_op_button_gating(state).unload_disabled;
}

void AmsContextMenu::handle_clear_spool() {
    spdlog::info("[AmsContextMenu] Clear spool requested for slot {}", get_item_index());
    dispatch_ams_action(MenuAction::CLEAR_SPOOL);
}

void AmsContextMenu::handle_spoolman() {
    spdlog::info("[AmsContextMenu] Spoolman select requested for slot {}", get_item_index());
    dispatch_ams_action(MenuAction::SPOOLMAN);
}

void AmsContextMenu::handle_scan_qr() {
    spdlog::info("[AmsContextMenu] Scan QR requested for slot {}", get_item_index());
    dispatch_ams_action(MenuAction::SCAN_QR);
}

// ============================================================================
// Static Callback Registration
// ============================================================================

void AmsContextMenu::register_callbacks() {
    if (callbacks_registered_) {
        return;
    }

    register_xml_callbacks({
        {"ams_context_backdrop_cb", on_backdrop_cb},
        {"ams_context_load_cb", on_load_cb},
        {"ams_context_unload_cb", on_unload_cb},
        {"ams_context_gate_select_cb", on_gate_select_cb},
        {"ams_context_gate_check_cb", on_gate_check_cb},
        {"ams_context_edit_cb", on_edit_cb},
        {"ams_context_clear_spool_cb", on_clear_spool_cb},
        {"ams_context_spoolman_cb", on_spoolman_cb},
        {"ams_context_open_source_cb", on_open_source_cb},
        {"ams_context_scan_qr_cb", on_scan_qr_cb},
        {"ams_context_tool_changed_cb", on_tool_changed_cb},
        {"ams_context_backup_changed_cb", on_backup_changed_cb},
    });

    callbacks_registered_ = true;
    spdlog::debug("[AmsContextMenu] Callbacks registered");
}

// ============================================================================
// Static Callbacks (Instance Lookup via Static Pointer)
// ============================================================================

AmsContextMenu* AmsContextMenu::get_active_instance() {
    if (!s_active_instance_) {
        spdlog::warn("[AmsContextMenu] No active instance for event");
    }
    return s_active_instance_;
}

void AmsContextMenu::on_backdrop_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_backdrop_clicked();
    }
}

void AmsContextMenu::on_load_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_load();
    }
}

void AmsContextMenu::on_unload_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_unload();
    }
}

void AmsContextMenu::on_gate_select_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_gate_select();
    }
}

void AmsContextMenu::on_gate_check_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_gate_check();
    }
}

void AmsContextMenu::on_edit_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_edit();
    }
}

void AmsContextMenu::on_clear_spool_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_clear_spool();
    }
}

void AmsContextMenu::on_open_source_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_open_source();
    }
}

void AmsContextMenu::on_spoolman_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_spoolman();
    }
}

void AmsContextMenu::on_scan_qr_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_scan_qr();
    }
}

void AmsContextMenu::on_tool_changed_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_tool_changed();
    }
}

void AmsContextMenu::on_backup_changed_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_backup_changed();
    }
}

// ============================================================================
// Dropdown Handlers
// ============================================================================

void AmsContextMenu::handle_tool_changed() {
    if (!tool_dropdown_ || !backend_) {
        return;
    }

    int selected = static_cast<int>(lv_dropdown_get_selected(tool_dropdown_));
    // Option 0 = "None", options 1+ = T0, T1, T2...
    int tool_number = selected - 1; // -1 = None

    spdlog::info("[AmsContextMenu] Tool mapping changed for slot {}: tool {}", get_item_index(),
                 tool_number >= 0 ? tool_number : -1);

    if (tool_number >= 0) {
        // Warn if another tool already maps to this slot
        auto mapping = backend_->get_tool_mapping();
        for (size_t i = 0; i < mapping.size(); ++i) {
            if (static_cast<int>(i) != tool_number && mapping[i] == get_item_index()) {
                spdlog::warn("[AmsContextMenu] Tool {} will share slot {} with tool {}",
                             tool_number, get_item_index(), i);
                std::string msg = fmt::format(lv_tr("T{} shares slot with T{}"), tool_number, i);
                ToastManager::instance().show(ToastSeverity::WARNING, msg.c_str());
                break;
            }
        }

        auto result = backend_->set_tool_mapping(tool_number, get_item_index());
        if (!result.success()) {
            // notify_ams_error() logs the technical message itself.
            helix::ui::notify_ams_error(result, lv_tr("Tool mapping failed"));
        }
    }
    // Note: "None" selection doesn't clear mapping - user needs to map another slot to that tool
}

void AmsContextMenu::handle_backup_changed() {
    if (!backup_dropdown_ || !backend_) {
        return;
    }

    int selected = static_cast<int>(lv_dropdown_get_selected(backup_dropdown_));

    // Convert dropdown index back to actual slot index
    // Dropdown: None=0, then all slots except current slot
    int backup_slot = -1; // Default to None
    if (selected > 0) {
        // Find the actual slot index by counting through slots (skipping current)
        int dropdown_idx = 0;
        for (int i = 0; i < total_slots_; ++i) {
            if (i != get_item_index()) {
                dropdown_idx++;
                if (dropdown_idx == selected) {
                    backup_slot = i;
                    break;
                }
            }
        }
    }

    // Validate material compatibility if a backup slot was selected
    if (backup_slot >= 0 && get_item_index() >= 0) {
        std::string current_material = backend_->get_slot_info(get_item_index()).material;
        std::string backup_material = backend_->get_slot_info(backup_slot).material;

        // Only check compatibility if both slots have materials set
        if (!current_material.empty() && !backup_material.empty() &&
            !filament::are_materials_compatible(current_material, backup_material)) {
            spdlog::warn("[AmsContextMenu] Incompatible backup: {} cannot use {} as backup",
                         current_material, backup_material);

            // Show toast error
            std::string msg =
                fmt::format(lv_tr("Incompatible materials: {} cannot use {} as backup"),
                            current_material, backup_material);
            ToastManager::instance().show(ToastSeverity::ERROR, msg.c_str());

            // Reset dropdown to "None" (index 0)
            lv_dropdown_set_selected(backup_dropdown_, 0);
            return;
        }
    }

    spdlog::info("[AmsContextMenu] Backup slot changed for slot {}: backup {}", get_item_index(),
                 backup_slot >= 0 ? backup_slot : -1);

    auto result = backend_->set_endless_spool_backup(get_item_index(), backup_slot);
    if (!result.success()) {
        spdlog::warn("[AmsContextMenu] Failed to set endless spool backup: {}", result.user_msg);
    } else {
        // Bump slots version to trigger endless spool arrow redraw
        AmsState::instance().bump_slots_version();
    }

    // Close the context menu after selection
    hide();
}

// ============================================================================
// Dropdown Configuration
// ============================================================================

void AmsContextMenu::configure_dropdowns() {
    if (!menu()) {
        return;
    }

    // Find dropdown widgets
    tool_dropdown_ = lv_obj_find_by_name(menu(), "tool_dropdown");
    backup_dropdown_ = lv_obj_find_by_name(menu(), "backup_dropdown");

    // Find row containers and divider
    lv_obj_t* tool_row = lv_obj_find_by_name(menu(), "tool_dropdown_row");
    lv_obj_t* backup_row = lv_obj_find_by_name(menu(), "backup_dropdown_row");
    lv_obj_t* divider = lv_obj_find_by_name(menu(), "dropdown_divider");

    bool show_any_dropdown = false;

    // Tool mapping dropdown - hidden until we have a good UX for remapping
    // (currently 1:1 lane-to-tool mapping is the only conflict-free option)
    // if (backend_) {
    //     auto tool_caps = backend_->get_tool_mapping_capabilities();
    //     if (tool_caps.supported) {
    //         populate_tool_dropdown();
    //         if (tool_row) {
    //             lv_obj_remove_flag(tool_row, LV_OBJ_FLAG_HIDDEN);
    //         }
    //         if (tool_dropdown_ && !tool_caps.editable) {
    //             lv_obj_add_state(tool_dropdown_, LV_STATE_DISABLED);
    //         }
    //         show_any_dropdown = true;
    //     }
    // }
    (void)tool_row;

    // Configure endless spool dropdown
    if (backend_) {
        auto es_caps = backend_->get_endless_spool_capabilities();
        if (es_caps.supported) {
            populate_backup_dropdown();
            if (backup_row) {
                lv_obj_remove_flag(backup_row, LV_OBJ_FLAG_HIDDEN);
            }
            // Disable dropdown if not editable
            if (backup_dropdown_ && !es_caps.editable) {
                lv_obj_add_state(backup_dropdown_, LV_STATE_DISABLED);
            }
            show_any_dropdown = true;
            spdlog::debug("[AmsContextMenu] Endless spool enabled (editable={})", es_caps.editable);
        }
    }

    // Show divider only if any dropdown is visible
    if (divider && show_any_dropdown) {
        lv_obj_remove_flag(divider, LV_OBJ_FLAG_HIDDEN);
    }
}

void AmsContextMenu::populate_tool_dropdown() {
    if (!tool_dropdown_) {
        return;
    }

    std::string options = build_tool_options();
    lv_dropdown_set_options(tool_dropdown_, options.c_str());

    int current_tool = get_current_tool_for_slot();
    // Map tool number to dropdown index: None=0, T0=1, T1=2, etc.
    int selected_index = (current_tool >= 0) ? (current_tool + 1) : 0;
    lv_dropdown_set_selected(tool_dropdown_, static_cast<uint32_t>(selected_index));

    spdlog::debug("[AmsContextMenu] Tool dropdown populated: slot {} maps to tool {}",
                  get_item_index(), current_tool);
}

void AmsContextMenu::populate_backup_dropdown() {
    if (!backup_dropdown_) {
        return;
    }

    std::string options = build_backup_options();
    lv_dropdown_set_options(backup_dropdown_, options.c_str());

    int current_backup = get_current_backup_for_slot();
    // Map backup slot to dropdown index, accounting for skipped current slot
    // Dropdown: None=0, then all slots except current slot
    int selected_index = 0; // Default to None
    if (current_backup >= 0) {
        // Count how many slots appear before the backup slot in the dropdown
        // (which skips the current slot)
        selected_index = 1; // Start after "None"
        for (int i = 0; i < current_backup; ++i) {
            if (i != get_item_index()) {
                selected_index++;
            }
        }
    }
    lv_dropdown_set_selected(backup_dropdown_, static_cast<uint32_t>(selected_index));

    spdlog::debug("[AmsContextMenu] Backup dropdown populated: slot {} backup is {}",
                  get_item_index(), current_backup);
}

std::string AmsContextMenu::build_tool_options() const {
    std::string options = lv_tr("None");
    // Add tool options T0, T1, T2... based on total slots
    for (int i = 0; i < total_slots_; ++i) {
        options += "\nT" + std::to_string(i);
    }
    return options;
}

std::string AmsContextMenu::build_backup_options() const {
    std::string options = lv_tr("None");

    // Get current slot's material for compatibility checking
    std::string current_material;
    if (backend_ && get_item_index() >= 0) {
        current_material = backend_->get_slot_info(get_item_index()).material;
    }

    // Add slot options Slot 1, Slot 2... based on total slots
    // Skip the current slot (can't be backup for itself)
    // Mark incompatible materials
    for (int i = 0; i < total_slots_; ++i) {
        if (i != get_item_index()) {
            // Same spool number the badges show — a backup list offering "Slot 8"
            // when no badge reads 8 names nothing the user can point at.
            const int shown = backend_ ? backend_->spool_display_number(i) : i + 1;
            std::string slot_option = "\n" + fmt::format(lv_tr("Slot {}"), shown);

            // Check material compatibility if we have a current material
            if (backend_ && !current_material.empty()) {
                std::string other_material = backend_->get_slot_info(i).material;
                if (!other_material.empty() &&
                    !filament::are_materials_compatible(current_material, other_material)) {
                    slot_option += std::string(" ") + lv_tr("(incompatible)");
                }
            }

            options += slot_option;
        }
    }
    return options;
}

int AmsContextMenu::get_current_tool_for_slot() const {
    if (!backend_) {
        return -1;
    }

    // Get tool mapping and find which tool maps to this slot
    auto mapping = backend_->get_tool_mapping();
    for (size_t tool = 0; tool < mapping.size(); ++tool) {
        if (mapping[tool] == get_item_index()) {
            return static_cast<int>(tool);
        }
    }
    return -1; // No tool maps to this slot
}

int AmsContextMenu::get_current_backup_for_slot() const {
    if (!backend_) {
        return -1;
    }

    auto configs = backend_->get_endless_spool_config();
    for (const auto& config : configs) {
        if (config.slot_index == get_item_index()) {
            return config.backup_slot;
        }
    }
    return -1; // No backup configured
}

} // namespace helix::ui
