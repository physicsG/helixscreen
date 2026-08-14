// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_toolhead_menu.h"

#include "ui_button.h"
#include "ui_callback_helpers.h"
#include "ui_error_reporting.h"

#include "ams_backend.h"
#include "ams_state.h"
#include "ams_types.h"
#include "app_globals.h"
#include "data_root_resolver.h"
#include "filament_op_slot_resolver.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include <cstdio>

namespace helix::ui {

namespace {

/// The slot that feeds virtual tool @p tool_index, or @p tool_index unchanged
/// when nothing maps to it.
///
/// The path canvas hands its click back as the VIRTUAL tool number shown on the
/// badge (ui_system_path_canvas.h says so explicitly), but every backend call
/// this menu makes is SLOT-indexed — including can_unload_from_toolhead(),
/// whose name says toolhead while its parameter is documented as a slot. The
/// two coincide on the U1, where slot N feeds tool N, and diverge on a
/// toolchanger under ASSIGN_TOOL remapping (see AmsBackend's note on tool
/// numbers vs slots), which is when a menu keyed on the wrong one acts on the
/// wrong head.
///
/// Falling back to the raw index preserves the identity behaviour for any
/// backend that publishes no mapped_tool at all.
int slot_for_tool(const AmsSystemInfo& info, int tool_index) {
    for (const auto& unit : info.units) {
        for (const auto& slot : unit.slots) {
            if (slot.mapped_tool == tool_index) {
                return slot.global_index;
            }
        }
    }
    return tool_index;
}

} // namespace

/// Test seam for slot_for_tool() — see the anonymous-namespace definition.
int slot_for_tool_for_test(const AmsSystemInfo& info, int tool_index) {
    return slot_for_tool(info, tool_index);
}

// Static member initialization
bool AmsToolheadMenu::callbacks_registered_ = false;
AmsToolheadMenu* AmsToolheadMenu::s_active_instance_ = nullptr;

// ============================================================================
// The rule (pure — no LVGL, no backend)
// ============================================================================

ToolheadMenuModel toolhead_menu_model(int tool_index, int mounted_tool, bool supports_park,
                                      bool slot_present, bool can_unload, bool print_blocks_ops,
                                      bool source_is_external) {
    ToolheadMenuModel m;
    if (tool_index < 0) {
        return m;
    }
    // Every entry moves the carriage or the filament, and the backend refuses
    // all four mid-print. Offering them anyway would be four buttons that each
    // answer with a toast; an empty model means no menu opens at all.
    if (print_blocks_ops) {
        return m;
    }
    const bool is_mounted = (tool_index == mounted_tool);

    // Select brings this head to the carriage; pointless for the head already
    // on it. Park docks whatever is on the carriage, so it belongs only to the
    // mounted head — and only where the backend can actually do it.
    m.show_select = !is_mounted;
    m.show_park = is_mounted && supports_park;

    // can_unload is "filament is at THIS toolhead", so it decides the pair:
    // offering Load beside it would offer to feed an already-fed head. A lane
    // with no filament at all offers neither.
    m.show_unload = can_unload;
    // A head fed from another unit has no Load of its own: the command names a
    // bay, and only that bay's menu can pick one. Withdrawn as a property of
    // the position, not a transient state -- see the header.
    m.show_load = slot_present && !can_unload && !source_is_external;
    return m;
}

// ============================================================================
// Construction / Destruction
// ============================================================================

AmsToolheadMenu::AmsToolheadMenu() {
    init_subjects();
    spdlog::debug("[AmsToolheadMenu] Constructed");
}

void AmsToolheadMenu::init_subjects() {
    if (subject_initialized_ || !lv_is_initialized()) {
        return;
    }
    lv_subject_init_int(&show_select_subject_, 0);
    lv_xml_register_subject(nullptr, "ams_toolhead_show_select", &show_select_subject_);
    lv_subject_init_int(&show_park_subject_, 0);
    lv_xml_register_subject(nullptr, "ams_toolhead_show_park", &show_park_subject_);
    lv_subject_init_int(&show_load_subject_, 0);
    lv_xml_register_subject(nullptr, "ams_toolhead_show_load", &show_load_subject_);
    lv_subject_init_int(&show_unload_subject_, 0);
    lv_xml_register_subject(nullptr, "ams_toolhead_show_unload", &show_unload_subject_);

    lv_subject_init_string(&title_subject_, title_buf_, nullptr, sizeof(title_buf_), "");
    lv_xml_register_subject(nullptr, "ams_toolhead_title", &title_subject_);

    subject_initialized_ = true;
}

AmsToolheadMenu::~AmsToolheadMenu() {
    // Clear active instance before base destructor calls hide()
    if (s_active_instance_ == this) {
        s_active_instance_ = nullptr;
    }
    // Tear the widgets down BEFORE the subjects they observe. ~ContextMenu would
    // otherwise hide() after this body has already deinit'd them, leaving the
    // card's bind_flag observers pointing at reclaimed subjects.
    hide();
    if (subject_initialized_ && lv_is_initialized()) {
        lv_subject_deinit(&show_select_subject_);
        lv_subject_deinit(&show_park_subject_);
        lv_subject_deinit(&show_load_subject_);
        lv_subject_deinit(&show_unload_subject_);
        lv_subject_deinit(&title_subject_);
        subject_initialized_ = false;
    }
    spdlog::trace("[AmsToolheadMenu] Destroyed");
}

// ============================================================================
// Public API
// ============================================================================

void AmsToolheadMenu::set_action_callback(ActionCallback callback) {
    action_callback_ = std::move(callback);
}

void AmsToolheadMenu::publish_model() {
    if (!subject_initialized_) {
        return;
    }
    lv_subject_set_int(&show_select_subject_, model_.show_select ? 1 : 0);
    lv_subject_set_int(&show_park_subject_, model_.show_park ? 1 : 0);
    lv_subject_set_int(&show_load_subject_, model_.show_load ? 1 : 0);
    lv_subject_set_int(&show_unload_subject_, model_.show_unload ? 1 : 0);

    snprintf(title_buf_, sizeof(title_buf_), "%s T%d", lv_tr("Toolhead"),
             tool_index_ >= 0 ? tool_index_ : 0);
    lv_subject_copy_string(&title_subject_, title_buf_);
}

bool AmsToolheadMenu::show_at(lv_obj_t* parent, lv_obj_t* anchor, lv_point_t click_pt,
                              int tool_index, AmsBackend* backend) {
    register_callbacks();
    init_subjects();

    backend_ = backend;
    tool_index_ = tool_index;

    // Ask the backend once, here, so the menu never re-queries mid-show and the
    // rule itself stays a pure function of those answers.
    int mounted = -1;
    bool supports_park = false;
    bool present = false;
    bool can_unload = false;
    int slot_index = tool_index;
    if (backend) {
        // One fetch: get_system_info() returns by value under the backend mutex.
        const AmsSystemInfo info = backend->get_system_info();
        mounted = info.mounted_tool; // tool-space, compared against tool_index
        slot_index = slot_for_tool(info, tool_index);
        supports_park = backend->supports_toolhead_park();
        present = backend->get_slot_info(slot_index).is_present();
        can_unload = backend->can_unload_from_toolhead(slot_index);
    }
    slot_index_ = slot_index;

    // Same predicate the per-slot menu greys its buttons with, and the mirror of
    // the backend's own refusal: PRINTING always blocks, PAUSED only on a
    // backend whose filament macro homes itself. Reading the raw print_active
    // subject instead would suppress the menu through the runout pause that is
    // precisely when these actions are needed.
    const auto job_state = static_cast<helix::PrintJobState>(
        lv_subject_get_int(get_printer_state().get_print_state_enum_subject()));
    const bool print_blocks_ops = helix::ui::print_blocks_filament_op(
        job_state == helix::PrintJobState::PRINTING, job_state == helix::PrintJobState::PAUSED,
        backend && backend->filament_ops_self_home());

    // Same question the per-slot menu asks: is this position's filament
    // described by another unit? On multiACE an ACE-fed head answers with the
    // ACE's unit index.
    const bool source_is_external =
        backend && backend->slot_identity_owner_unit(slot_index).has_value();

    model_ = toolhead_menu_model(tool_index, mounted, supports_park, present, can_unload,
                                 print_blocks_ops, source_is_external);

    if (toolhead_menu_is_empty(model_)) {
        // An empty parked head on a backend that cannot park: there is no action
        // to offer, and an empty card reads as a bug. Say nothing.
        spdlog::debug("[AmsToolheadMenu] No actions available for T{} — not showing", tool_index);
        return false;
    }

    publish_model();

    s_active_instance_ = this;
    set_click_point(click_pt);

    bool result = show_near_widget(parent, tool_index, anchor);
    if (!result) {
        s_active_instance_ = nullptr;
    }

    spdlog::debug("[AmsToolheadMenu] Shown for T{} (select={} park={} load={} unload={})",
                  tool_index, model_.show_select, model_.show_park, model_.show_load,
                  model_.show_unload);
    return result;
}

// ============================================================================
// ContextMenu override
// ============================================================================

void AmsToolheadMenu::on_created(lv_obj_t* menu_obj) {
    // Name the head on the button that acts on it. ui_button_set_text is the
    // widget's own setter (the custom-XML-widget exception), not a raw
    // lv_label_set_text on someone else's label.
    if (lv_obj_t* btn = lv_obj_find_by_name(menu_obj, "btn_select_tool")) {
        char label[32];
        snprintf(label, sizeof(label), "%s T%d", lv_tr("Select"),
                 tool_index_ >= 0 ? tool_index_ : 0);
        ui_button_set_text(btn, label);
    }
}

// ============================================================================
// Event Handlers
// ============================================================================

void AmsToolheadMenu::dispatch_toolhead_action(ToolheadAction action) {
    ActionCallback callback_copy = action_callback_;
    const int tool = tool_index_;

    if (s_active_instance_ == this) {
        s_active_instance_ = nullptr;
    }
    hide();

    if (callback_copy) {
        callback_copy(action, tool);
    }
}

void AmsToolheadMenu::handle_backdrop_clicked() {
    spdlog::debug("[AmsToolheadMenu] Backdrop clicked");
    dispatch_toolhead_action(ToolheadAction::CANCELLED);
}

void AmsToolheadMenu::handle_select() {
    spdlog::info("[AmsToolheadMenu] Select T{} requested", tool_index_);
    dispatch_toolhead_action(ToolheadAction::SELECT);
}

void AmsToolheadMenu::handle_park() {
    spdlog::info("[AmsToolheadMenu] Park requested (T{} mounted)", tool_index_);
    dispatch_toolhead_action(ToolheadAction::PARK);
}

void AmsToolheadMenu::handle_load() {
    spdlog::info("[AmsToolheadMenu] Load T{} requested", tool_index_);
    dispatch_toolhead_action(ToolheadAction::LOAD);
}

void AmsToolheadMenu::handle_unload() {
    spdlog::info("[AmsToolheadMenu] Unload T{} requested", tool_index_);
    dispatch_toolhead_action(ToolheadAction::UNLOAD);
}

// ============================================================================
// Static Callback Registration
// ============================================================================

void AmsToolheadMenu::register_callbacks() {
    if (callbacks_registered_) {
        return;
    }

    // The menu owns its own component registration rather than relying on a
    // panel to have done it. Two panels open this menu now, and the overview is
    // reachable without ever visiting the detail panel — which is exactly how
    // this shipped broken: the XML was registered only in AmsPanel, so a tap on
    // the overview logged "not a known widget/element/component" and no menu
    // appeared. Registration is idempotent and this whole function runs once.
    lv_xml_register_component_from_file(
        helix::asset_component_uri("ui_xml/ams_toolhead_menu.xml").c_str());

    register_xml_callbacks({
        {"ams_toolhead_backdrop_cb", on_backdrop_cb},
        {"ams_toolhead_select_cb", on_select_cb},
        {"ams_toolhead_park_cb", on_park_cb},
        {"ams_toolhead_load_cb", on_load_cb},
        {"ams_toolhead_unload_cb", on_unload_cb},
    });

    callbacks_registered_ = true;
    spdlog::debug("[AmsToolheadMenu] Callbacks registered");
}

// ============================================================================
// Static Callbacks (Instance Lookup via Static Pointer)
// ============================================================================

AmsToolheadMenu* AmsToolheadMenu::get_active_instance() {
    if (!s_active_instance_) {
        spdlog::warn("[AmsToolheadMenu] No active instance for event");
    }
    return s_active_instance_;
}

void AmsToolheadMenu::on_backdrop_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_backdrop_clicked();
    }
}

void AmsToolheadMenu::on_select_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_select();
    }
}

void AmsToolheadMenu::on_park_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_park();
    }
}

void AmsToolheadMenu::on_load_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_load();
    }
}

void AmsToolheadMenu::on_unload_cb(lv_event_t* /*e*/) {
    auto* self = get_active_instance();
    if (self) {
        self->handle_unload();
    }
}

// ============================================================================
// Shared dispatch
// ============================================================================

void dispatch_toolhead_menu_action(AmsToolheadMenu::ToolheadAction action, int tool_index) {
    using TA = AmsToolheadMenu::ToolheadAction;
    if (action == TA::CANCELLED) {
        return;
    }
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        return;
    }

    // Same conversion as show_at(): the caller passes the VIRTUAL tool number,
    // and select_slot/load_filament/unload_filament are all slot-indexed.
    const int slot_index = slot_for_tool(backend->get_system_info(), tool_index);

    AmsError err{};
    switch (action) {
    case TA::SELECT:
        // select_slot() on a toolchanger IS the tool change (`T{n}`) -- see
        // AmsBackendSnapmaker::select_slot_moves_toolhead().
        err = backend->select_slot(slot_index);
        break;
    case TA::PARK:
        // Takes no index at all: the firmware parks whatever is on the carriage.
        err = backend->park_toolhead();
        break;
    case TA::LOAD:
        err = backend->load_filament(slot_index);
        break;
    case TA::UNLOAD:
        err = backend->unload_filament(slot_index);
        break;
    case TA::CANCELLED:
        return;
    }

    if (err.result != AmsResult::SUCCESS) {
        notify_ams_error(err, lv_tr("Toolhead command failed"));
    }
}

} // namespace helix::ui
