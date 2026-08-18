// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_context_menu.h"

#include "ams_types.h"

#include <functional>
#include <lvgl.h>
#include <memory>

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
 * Mirrors AmsSelectorMenu (the hub/selector menu): ContextMenu::active_as<>()
 * callback dispatch, the shared backdrop, capability gating at show time. This
 * class only *presents* the menu and reports the choice via a callback; the
 * canvas→panel→menu→backend wiring is show_toolhead_menu_at_touch() and
 * dispatch_toolhead_menu_action() below, shared by every panel that opens it.
 *
 * ## The two index spaces
 *
 * Both canvases hand back THE TOOL NUMBER ON THE BADGE the user tapped — the
 * system-path canvas has always said so, and the filament-path canvas now
 * reports the same rule its badge draws with (`mapped_tool` when it was given
 * one, else the lane). Every backend call the menu makes is SLOT-indexed, so
 * the tool is resolved to a slot ONCE, at show time, through the same rule the
 * filament panel's op buttons use (toolhead_slot_for_tool), and that slot
 * travels with the action. The menu used to reverse-map a number that one
 * canvas reported as a lane and the other as a tool, which under ASSIGN_TOOL
 * remapping acted on a different head than the one tapped.
 *
 * ## Only on a toolchanger
 *
 * show_at() refuses (returns false) unless the backend's topology is PARALLEL.
 * Every entry is a carriage or per-head operation: on a hub or selector backend
 * `mounted_tool` is a concept that does not exist (always -1), so the rule
 * offered "Select" — which on Happy Hare drives the selector, on an ACE runs a
 * full filament feed, and on CFS/AFC is a guaranteed refusal — from a nozzle
 * tap that had been inert before the menu existed.
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
 * @param source_is_external This head's filament comes from another unit, so
 *        there is no "load" that names a source here: an ACE-fed U1 head loads
 *        with `ACE_LOAD_HEAD HEAD=n ACE=a SLOT=s`, which picks a specific bay.
 *        The choice belongs to that bay's menu on the ACE's own page. Unload
 *        needs no bay (`ACE_UNLOAD_HEAD HEAD=n`) and stays offered, which is
 *        the asymmetry this models -- the same one the per-slot menu applies
 *        via AmsContextMenu::decide_can_load()'s source_external.
 * @param print_blocks_ops A running print refuses toolhead motion
 *        (helix::ui::print_blocks_filament_op). EVERY entry here moves the
 *        carriage or the filament, so this empties the menu outright rather
 *        than offering four buttons that would each answer with a refusal.
 *        A PAUSED job does not block on this backend — that pause is the
 *        runout-recovery workflow these actions exist for.
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
                                                    bool can_unload, bool print_blocks_ops = false,
                                                    bool source_is_external = false);

/**
 * @brief The global slot a toolhead-menu tool number acts on.
 *
 * resolve_op_button_slot() with its toolchanger arm pinned: this menu only
 * opens on a PARALLEL canvas, where a tool number IS a lane when no
 * tool_to_slot_map entry says otherwise. (The resolver's single-tool arm
 * answers current_slot — the loaded lane of a multi-lane AMS — which is never
 * what a nozzle tap means.) One rule with the filament panel's op buttons, so
 * the menu and the buttons can never name different slots for one tool;
 * ams_tool_map_sync.h records the two encodings shipping out of step twice.
 */
[[nodiscard]] int toolhead_slot_for_tool(const AmsSystemInfo& info, int tool_index);

/// True when the model has no entries — nothing to show, so show nothing.
[[nodiscard]] inline bool toolhead_menu_is_empty(const ToolheadMenuModel& m) {
    return !m.show_select && !m.show_park && !m.show_load && !m.show_unload;
}

class AmsToolheadMenu : public ContextMenu {
    HELIX_CONTEXT_MENU_KIND(AmsToolheadMenu)

  public:
    enum class ToolheadAction {
        CANCELLED, ///< User dismissed menu without action
        SELECT,    ///< Mount this head (T{n})
        PARK,      ///< Dock the mounted head (PARK_EXTRUDER)
        LOAD,      ///< Load filament into this head
        UNLOAD     ///< Unload filament from this head
    };

    /// @param tool_index the head, as the badge numbers it
    /// @param slot_index the slot resolved for it at show time — what every
    ///        backend call takes. Carried with the action so the dispatch acts
    ///        on exactly the slot the menu was shown for.
    using ActionCallback =
        std::function<void(ToolheadAction action, int tool_index, int slot_index)>;

    AmsToolheadMenu();
    ~AmsToolheadMenu() override;

    // Non-copyable
    AmsToolheadMenu(const AmsToolheadMenu&) = delete;
    AmsToolheadMenu& operator=(const AmsToolheadMenu&) = delete;

    // Non-movable. Both panels hold this in a unique_ptr and nothing moves it.
    AmsToolheadMenu(AmsToolheadMenu&&) = delete;
    AmsToolheadMenu& operator=(AmsToolheadMenu&&) = delete;

    /**
     * @brief Show the toolhead context menu near the tapped nozzle
     * @param parent Parent screen for the menu
     * @param anchor Widget to position the menu near (the canvas)
     * @param click_pt Display-coordinate click point (for positioning)
     * @param tool_index Head that was tapped, as the badge numbers it
     * @param backend Backend pointer for capability gating
     * @return true if the menu was shown; false if the backend is not a
     *         toolchanger (PARALLEL topology) or the head has no entries to offer
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
    // menu_card_name(): the default, "context_menu" -- the card in the XML
    // carries that name so the base can size and position it.
    void on_created(lv_obj_t* menu_obj) override;
    /// A tap outside the card cancels. The base owns the backdrop callback.
    void on_backdrop_clicked() override;

  private:
    ActionCallback action_callback_;
    AmsBackend* backend_ = nullptr;
    int tool_index_ = -1; ///< the badge's number, for the title and Select label
    int slot_index_ = -1; ///< ...and the slot resolved for it -- see toolhead_slot_for_tool()
    ToolheadMenuModel model_;

    // Entry visibility is published as subjects and bound with <bind_flag_if_eq>
    // in the XML, rather than hidden from C++ — declarative rule 2.
    //
    // ONE set for the class, not one per instance. The XML binds these by fixed
    // name, and both the AMS panel and the overview own an instance: with
    // per-instance subjects registered under the same names, whichever instance
    // registered LAST owned the names, and the other's publish_model() wrote to
    // subjects no card was bound to any more -- a blank or wrong-buttoned menu,
    // reachable on every multi-unit rig. One menu is on screen at a time, so
    // one set of subjects is exactly what the XML needs. Initialised once and
    // torn down through StaticSubjectRegistry like every other static subject.
    static lv_subject_t s_show_select_subject_; ///< 1 = offer "Select T{n}"
    static lv_subject_t s_show_park_subject_;   ///< 1 = offer "Park"
    static lv_subject_t s_show_load_subject_;   ///< 1 = offer "Load"
    static lv_subject_t s_show_unload_subject_; ///< 1 = offer "Unload"
    static lv_subject_t s_title_subject_;       ///< Card header, e.g. "Toolhead T3"
    static char s_title_buf_[32];
    static bool s_subjects_initialized_;

    static void init_subjects();
    static void deinit_subjects();
    void publish_model();

    /**
     * @brief Common pattern: hide, then invoke the callback
     */
    void dispatch_toolhead_action(ToolheadAction action);

    // === Event Handlers ===
    void handle_select();
    void handle_park();
    void handle_load();
    void handle_unload();

    // === Static Callback Registration ===
    static void register_callbacks();
    static bool callbacks_registered_;

    // === Static Callbacks (instance lookup via ContextMenu::active_as<>) ===
    /// ContextMenu::active_as() that also logs the unexpected empty case.
    static AmsToolheadMenu* get_active_instance();
    static void on_select_cb(lv_event_t* e);
    static void on_park_cb(lv_event_t* e);
    static void on_load_cb(lv_event_t* e);
    static void on_unload_cb(lv_event_t* e);
};

/**
 * @brief Run a toolhead menu action against the live backend.
 *
 * Shared by every panel that opens the menu — the AMS detail panel and the
 * overview — because the dispatch is pure backend work with no panel state in
 * it, and two copies would drift the moment one of them grew a case.
 * CANCELLED and a missing backend are no-ops; a failure raises the standard AMS
 * error toast. @p slot_index is the slot the menu resolved at show time.
 */
void dispatch_toolhead_menu_action(AmsToolheadMenu::ToolheadAction action, int tool_index,
                                   int slot_index);

/**
 * @brief Everything between a canvas nozzle tap and the menu appearing.
 *
 * Range-checks the tool, reads the live touch point while the indev still
 * reports it, lazily builds the panel's menu instance, wires the shared
 * dispatch, shows. Both panels had a ~40-line copy of this, and a range check
 * had already been lost once in the copying.
 *
 * @param menu The panel's instance, created on first use and reused: a menu
 *        holds its own widget tree between shows.
 * @param parent_screen Screen the menu is raised on
 * @param canvas The canvas that was tapped (positioning anchor)
 * @param tool_index The tool number the canvas reported — the badge's
 * @return true if a menu was shown
 */
bool show_toolhead_menu_at_touch(std::unique_ptr<AmsToolheadMenu>& menu, lv_obj_t* parent_screen,
                                 lv_obj_t* canvas, int tool_index);

} // namespace helix::ui
