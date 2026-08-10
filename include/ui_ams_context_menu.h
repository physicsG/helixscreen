// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_context_menu.h"

#include "ams_types.h"

#include <functional>
#include <lvgl.h>
#include <optional>
#include <string>

// Forward declaration
class AmsBackend;
class AmsContextMenuTestAccess;

namespace helix::ui {

/**
 * @file ui_ams_context_menu.h
 * @brief Context menu for AMS slot operations
 *
 * Displays a popup menu near a slot with options to load, unload,
 * edit, or assign a Spoolman spool. Automatically positions itself
 * relative to the target slot widget.
 *
 * Extends the generic ContextMenu with AMS-specific features:
 * - Slot loaded/can-load subjects for button states
 * - Tool mapping dropdown
 * - Endless spool backup dropdown
 *
 * ## Usage:
 * @code
 * helix::ui::AmsContextMenu menu;
 * menu.set_action_callback([](MenuAction action, int slot_index) {
 *     switch (action) {
 *         case MenuAction::LOAD: // load filament...
 *         case MenuAction::UNLOAD: // unload filament...
 *         case MenuAction::EDIT: // show edit modal...
 *         case MenuAction::SPOOLMAN: // show spoolman picker...
 *     }
 * });
 * menu.show_near_widget(parent, slot_index, slot_widget);
 * @endcode
 */
class AmsContextMenu : public ContextMenu {
    friend class ::AmsContextMenuTestAccess;

  public:
    enum class MenuAction {
        CANCELLED,        ///< User dismissed menu without action
        LOAD,             ///< Load filament from this slot
        UNLOAD,           ///< Unload filament from toolhead
        EJECT,            ///< Eject filament from lane (release spool)
        RECOVER_POSITION, ///< Retract filament stranded past the hub back into the lane
        SELECT_GATE,      ///< Select this gate as the active gate (Happy Hare)
        CHECK_GATE,       ///< Check filament state of this gate (Happy Hare)
        EDIT,             ///< Edit slot properties
        CLEAR_SPOOL,      ///< Clear assigned spool from empty slot
        SPOOLMAN,         ///< Assign Spoolman spool
        OPEN_SOURCE_UNIT, ///< Jump to the unit that owns this slot's spool identity
        SCAN_QR           ///< Scan QR code to assign spool
    };

    using ActionCallback = std::function<void(MenuAction action, int slot_index)>;

    AmsContextMenu();
    ~AmsContextMenu() override;

    // Non-copyable
    AmsContextMenu(const AmsContextMenu&) = delete;
    AmsContextMenu& operator=(const AmsContextMenu&) = delete;

    // Movable
    AmsContextMenu(AmsContextMenu&& other) noexcept;
    AmsContextMenu& operator=(AmsContextMenu&& other) noexcept;

    /**
     * @brief Show context menu near a slot widget
     * @param parent Parent screen for the menu
     * @param slot_index Slot this menu is for (0-based)
     * @param near_widget Widget to position menu near (typically slot widget)
     * @param is_loaded True if the slot is loaded/active (enables Unload, suppresses Load)
     * @param backend Optional backend pointer for tool mapping/endless spool features
     * @return true if menu was shown successfully
     */
    bool show_near_widget(lv_obj_t* parent, int slot_index, lv_obj_t* near_widget,
                          bool is_loaded = false, AmsBackend* backend = nullptr);

    /**
     * @brief Show context menu for external spool (bypass/direct feed)
     *
     * Shows a reduced menu with only EDIT and CLEAR_SPOOL actions
     * (no LOAD/UNLOAD/EJECT since external spool is not managed by backend).
     *
     * @param parent Parent screen for the menu
     * @param anchor_widget Widget to position menu near (for click point)
     * @return true if menu was shown successfully
     */
    bool show_for_external_spool(lv_obj_t* parent, lv_obj_t* anchor_widget);

    /**
     * @brief Get slot index the menu is currently shown for
     */
    [[nodiscard]] int get_slot_index() const {
        return get_item_index();
    }

    /**
     * @brief Set callback for menu actions
     */
    void set_action_callback(ActionCallback callback);

  protected:
    const char* xml_component_name() const override {
        return "ams_context_menu";
    }
    void on_created(lv_obj_t* menu_obj) override;

  private:
    // === AMS-specific state ===
    ActionCallback action_callback_;

    /**
     * @brief Common pattern: clear static instance, hide, invoke callback
     */
    void dispatch_ams_action(MenuAction action);

    // === Subjects for button enable/disable states ===
    lv_subject_t slot_is_loaded_subject_; ///< 1 = loaded (Unload enabled), 0 = not loaded
    lv_subject_t slot_can_load_subject_;  ///< 1 = has filament (Load enabled), 0 = empty
    /// 1 = another unit owns this slot's filament identity (multiACE: an
    /// ACE-fed U1 head). Bound in XML so the edit actions hide and the
    /// "open the owner" action appears, without adding imperative visibility.
    lv_subject_t slot_source_external_subject_;
    /// Unit that owns this slot's identity, or -1. Held so OPEN_SOURCE_UNIT can
    /// name it without re-querying a backend that may have changed.
    int source_owner_unit_ = -1;
    bool subject_initialized_ = false;

    // === Backend reference for dropdown operations ===
    AmsBackend* backend_ = nullptr;
    int total_slots_ = 0;

    // === Dropdown widget pointers ===
    lv_obj_t* tool_dropdown_ = nullptr;
    lv_obj_t* backup_dropdown_ = nullptr;

    // === Pending state for on_created ===
    // True when the slot is loaded/active per backend->can_unload_from_toolhead().
    // Gates BOTH the Unload action (enabled) and the Load action (suppressed) —
    // a slot the firmware considers seated should not offer Load.
    bool pending_is_loaded_ = false;

    /// Which operation the Unload button performs for the open slot. Selected in
    /// on_created() from live backend state; drives both label and dispatch.
    enum class UnloadMode {
        Unload,          ///< Heated unload from the toolhead
        RecoverPosition, ///< Retract filament stranded past the hub (AFC)
        Eject,           ///< Cold retract of lane filament to the spool
        ForceEject,      ///< Presence-ignoring retract of an empty lane (AD5X)
        Unavailable,     ///< Nothing to do for this slot
    };
    UnloadMode unload_mode_ = UnloadMode::Unavailable;

    bool external_spool_mode_ = false; ///< True when showing menu for external spool (bypass)

    // === Event Handlers ===
    void handle_backdrop_clicked();
    void handle_load();
    void handle_unload();
    void handle_gate_select();
    void handle_gate_check();
    void handle_edit();
    void handle_clear_spool();
    void handle_spoolman();
    /// "Open in <unit>" — the slot's spool is described by another unit.
    void handle_open_source();
    void handle_scan_qr();
    void handle_tool_changed();
    void handle_backup_changed();

    // === Dropdown Configuration ===
    void configure_dropdowns();
    void populate_tool_dropdown();
    void populate_backup_dropdown();
    std::string build_tool_options() const;
    std::string build_backup_options() const;
    int get_current_tool_for_slot() const;
    int get_current_backup_for_slot() const;

    // === Static Callback Registration ===
    static void register_callbacks();
    // Pure: whether the context menu should offer "Clear Spool" for this slot.
    //
    // Deliberately independent of whether filament is physically present. The
    // affordance was previously gated on an EMPTY slot, so it disappeared as soon
    // as a spool went in — exactly when a stale assignment does damage, since that
    // is when the wrong metadata is printed with and when an edit aims a Spoolman
    // write at the previous spool. An empty lane's stale metadata is cosmetic.
    static bool should_show_clear_spool(const SlotInfo& slot);

    // Pure: selects the Unload button's operation for the open slot.
    //
    // Order encodes a deliberate priority ruling (see call site in on_created()):
    // a confidently-attributed stranded lane outranks Eject, but an unattributed
    // one (some backends share one physical sensor across every lane on a unit,
    // so "can recover" can be true for every lane at once with no way to say
    // whose filament tripped it) defers to Eject so a seated lane keeps its
    // Eject button — the unattributed Recover only catches lanes with nothing
    // left to eject.
    //
    // @param toolhead_unload      Slot unloads via the heated toolhead path
    // @param can_recover          backend_->can_recover_lane_position(slot_index)
    // @param recovery_attributed  backend_->lane_recovery_is_attributed()
    // @param supports_eject       backend_->supports_lane_eject()
    // @param slot_has_filament    SlotInfo::is_present() for this slot
    // @param supports_force_eject backend_->supports_force_eject()
    // @param slot_empty           !slot_has_filament
    static UnloadMode decide_unload_mode(bool toolhead_unload, bool can_recover,
                                         bool recovery_attributed, bool supports_eject,
                                         bool slot_has_filament, bool supports_force_eject,
                                         bool slot_empty);

    // Pure: whether the Load button is offered for the open slot.
    //
    // A thin adapter over helix::ui::compute_op_button_gating() — the one rule
    // the filament panel and the AMS sidebar answer from too. Kept as a named
    // predicate because the menu's inputs need translating: `toolhead_unload` is
    // the narrowed loaded signal (not the broadened recovery one), and presence
    // arrives as a tri-state so an UNKNOWN lane is not read as empty.
    //
    // `print_blocks_op` is helix::ui::print_blocks_filament_op(), the mirror of
    // AmsSubscriptionBackend::refuse_if_printing(). Do NOT pass the raw
    // print_active subject: PRINTING always refuses, but a PAUSED print now
    // ALLOWS the op on every backend whose filament macro does not home itself
    // (only AD5X IFS does). Greying the paused case is the bug in both
    // directions — offering what will be refused strands a runout-paused user
    // (bundle JX2FVRB9), and refusing what the backend accepts hides the
    // pause-then-swap recovery Klipper just told them to perform.
    static bool decide_can_load(bool system_busy, bool toolhead_unload,
                                std::optional<bool> slot_has_filament, bool print_blocks_op);

    // Pure: whether the Unload button is offered for the open slot.
    //
    // Only the heated toolhead unload is subject to the print gate; the cold lane
    // ops (Eject / RecoverPosition / ForceEject) do not move the toolhead and the
    // backend permits them via check_preconditions(false), which never consults
    // print state at all. Blocking the whole button would over-refuse and strand
    // filament the user could have ejected. That asymmetry is expressed to the
    // shared rule as OpButtonState::unload_is_cold_lane_op.
    //
    // `print_blocks_op`: see decide_can_load above — the computed predicate, not
    // the raw print_active subject.
    //
    // `cold_ops_print_gated` is AmsBackend::cold_lane_ops_refused_during_print():
    // true on a backend whose firmware refuses the cold ops mid-print too (AFC's
    // cmd_LANE_UNLOAD has its own is_printing() guard), which withdraws the
    // exemption above rather than offering a button into a certain refusal.
    // Required, not defaulted — a silently omitted `false` re-offers exactly the
    // dead-end button this parameter exists to remove.
    static bool decide_unload_enabled(bool system_busy, UnloadMode mode, bool print_blocks_op,
                                      bool cold_ops_print_gated);

    static bool callbacks_registered_;

    // === Static Callbacks ===
    static AmsContextMenu* s_active_instance_;
    static AmsContextMenu* get_active_instance();
    static void on_backdrop_cb(lv_event_t* e);
    static void on_load_cb(lv_event_t* e);
    static void on_unload_cb(lv_event_t* e);
    static void on_gate_select_cb(lv_event_t* e);
    static void on_gate_check_cb(lv_event_t* e);
    static void on_edit_cb(lv_event_t* e);
    static void on_clear_spool_cb(lv_event_t* e);
    static void on_spoolman_cb(lv_event_t* e);
    static void on_open_source_cb(lv_event_t* e);
    static void on_scan_qr_cb(lv_event_t* e);
    static void on_tool_changed_cb(lv_event_t* e);
    static void on_backup_changed_cb(lv_event_t* e);
};

} // namespace helix::ui
