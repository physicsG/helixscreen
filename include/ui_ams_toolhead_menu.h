// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_context_menu.h"

#include <functional>
#include <lvgl.h>

// Forward declaration
class AmsBackend;

namespace helix::ui {

/**
 * @file ui_ams_toolhead_menu.h
 * @brief Per-toolhead context menu for the filament-path canvas
 *
 * Opened by tapping a nozzle on a PARALLEL (tool changer) canvas. The nozzle
 * and the spool above it ask different questions — "which head is on the
 * carriage" versus "what filament is in this lane" — and before this menu both
 * regions opened the per-slot AmsContextMenu, so mounting or parking a head had
 * no UI at all.
 *
 * Mirrors AmsSelectorMenu (the hub/selector menu): static-instance callback
 * dispatch, capability gating in on_created(), and ContextMenu positioning.
 * This class only *presents* the menu and reports the choice via a callback;
 * the canvas→panel→menu→backend dispatch wiring lives in the panel.
 *
 * ## Usage:
 * @code
 * helix::ui::AmsToolheadMenu menu;
 * menu.set_action_callback([](ToolheadAction action, int tool) {
 *     switch (action) {
 *         case ToolheadAction::SELECT: // backend->select_slot(tool)...
 *         case ToolheadAction::PARK:   // backend->park_toolhead()...
 *     }
 * });
 * menu.show_at(parent, anchor, click_pt, tool_index, backend);
 * @endcode
 */

/**
 * @brief Which entries a toolhead menu should offer for one head.
 *
 * Extracted as a plain struct + free function so the rule is testable without
 * LVGL, a display, or a backend — see tests/unit/test_ams_toolhead_menu_model.cpp.
 */
struct ToolheadMenuModel {
    bool show_select = false; ///< "Select T{n}" — bring this head to the carriage
    bool show_park = false;   ///< "Park" — dock the head currently on the carriage
    bool show_load = false;   ///< "Load" — feed this head
    bool show_unload = false; ///< "Unload" — retract from this head
};

/**
 * @brief Decide the toolhead menu's entries.
 *
 * @param tool_index    The head that was tapped (0-based)
 * @param mounted_tool  Head currently on the carriage, or -1 for none
 * @param supports_park Backend answers supports_toolhead_park()
 * @param slot_present  This head's lane holds filament (filament_exist)
 * @param can_unload    Backend answers can_unload_from_toolhead(tool_index)
 *
 * Select and Park are mutually exclusive by construction — a head is either on
 * the carriage or it is not. Load and Unload likewise: can_unload is defined as
 * "filament is at THIS toolhead", so offering Load alongside it would be
 * offering to feed a head that is already fed. Both pairs can be entirely
 * absent (an empty parked head with no park support offers nothing at all),
 * which the caller must treat as "do not show a menu".
 */
[[nodiscard]] ToolheadMenuModel toolhead_menu_model(int tool_index, int mounted_tool,
                                                    bool supports_park, bool slot_present,
                                                    bool can_unload);

/// True when the model has no entries — nothing to show, so show nothing.
[[nodiscard]] inline bool toolhead_menu_is_empty(const ToolheadMenuModel& m) {
    return !m.show_select && !m.show_park && !m.show_load && !m.show_unload;
}

class AmsToolheadMenu : public ContextMenu {
  public:
    enum class ToolheadAction {
        CANCELLED, ///< User dismissed menu without action
        SELECT,    ///< Mount this head (T{n})
        PARK,      ///< Dock the mounted head (PARK_EXTRUDER)
        LOAD,      ///< Load filament into this head
        UNLOAD     ///< Unload filament from this head
    };

    using ActionCallback = std::function<void(ToolheadAction action, int tool_index)>;

    AmsToolheadMenu();
    ~AmsToolheadMenu() override;

    // Non-copyable
    AmsToolheadMenu(const AmsToolheadMenu&) = delete;
    AmsToolheadMenu& operator=(const AmsToolheadMenu&) = delete;

    // Movable
    AmsToolheadMenu(AmsToolheadMenu&& other) noexcept;
    AmsToolheadMenu& operator=(AmsToolheadMenu&& other) noexcept;

    /**
     * @brief Show the toolhead context menu near the tapped nozzle
     * @param parent Parent screen for the menu
     * @param anchor Widget to position the menu near (the canvas)
     * @param click_pt Display-coordinate click point (for positioning)
     * @param tool_index Head that was tapped (0-based)
     * @param backend Backend pointer for capability gating
     * @return true if the menu was shown; false if it had no entries to offer
     */
    bool show_at(lv_obj_t* parent, lv_obj_t* anchor, lv_point_t click_pt, int tool_index,
                 AmsBackend* backend);

    /**
     * @brief Set callback for menu actions
     */
    void set_action_callback(ActionCallback callback);

  protected:
    const char* xml_component_name() const override {
        return "ams_toolhead_menu";
    }
    const char* menu_card_name() const override {
        return "toolhead_menu";
    }
    void on_created(lv_obj_t* menu_obj) override;

  private:
    ActionCallback action_callback_;
    AmsBackend* backend_ = nullptr;
    int tool_index_ = -1;
    ToolheadMenuModel model_;

    // Entry visibility is published as subjects and bound with <bind_flag_if_eq>
    // in the XML, rather than hidden from C++ — declarative rule 2, and the
    // imperative-UI gate is a ratchet that this would otherwise push upward.
    lv_subject_t show_select_subject_; ///< 1 = offer "Select T{n}"
    lv_subject_t show_park_subject_;   ///< 1 = offer "Park"
    lv_subject_t show_load_subject_;   ///< 1 = offer "Load"
    lv_subject_t show_unload_subject_; ///< 1 = offer "Unload"
    lv_subject_t title_subject_;       ///< Card header, e.g. "Toolhead T3"
    char title_buf_[32] = {0};
    bool subject_initialized_ = false;

    void init_subjects();
    void publish_model();

    /**
     * @brief Common pattern: clear static instance, hide, invoke callback
     */
    void dispatch_toolhead_action(ToolheadAction action);

    // === Event Handlers ===
    void handle_backdrop_clicked();
    void handle_select();
    void handle_park();
    void handle_load();
    void handle_unload();

    // === Static Callback Registration ===
    static void register_callbacks();
    static bool callbacks_registered_;

    // === Static Callbacks (instance lookup via static pointer) ===
    static AmsToolheadMenu* s_active_instance_;
    static AmsToolheadMenu* get_active_instance();
    static void on_backdrop_cb(lv_event_t* e);
    static void on_select_cb(lv_event_t* e);
    static void on_park_cb(lv_event_t* e);
    static void on_load_cb(lv_event_t* e);
    static void on_unload_cb(lv_event_t* e);
};

} // namespace helix::ui
