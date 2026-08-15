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
#include "static_subject_registry.h"

#include <spdlog/spdlog.h>

#include <cstdio>

namespace helix::ui {

// Static member initialization
bool AmsToolheadMenu::callbacks_registered_ = false;
lv_subject_t AmsToolheadMenu::s_show_select_subject_;
lv_subject_t AmsToolheadMenu::s_show_park_subject_;
lv_subject_t AmsToolheadMenu::s_show_load_subject_;
lv_subject_t AmsToolheadMenu::s_show_unload_subject_;
lv_subject_t AmsToolheadMenu::s_title_subject_;
char AmsToolheadMenu::s_title_buf_[32] = {0};
bool AmsToolheadMenu::s_subjects_initialized_ = false;

// ============================================================================
// The rules (pure — no LVGL, no backend)
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

int toolhead_slot_for_tool(const AmsSystemInfo& info, int tool_index) {
    // ">1 == toolchanger" in resolve_op_button_slot()'s terms. Pinned rather
    // than read off ToolState: this surface only exists on a toolchanger, and
    // ToolState is not initialised in every test that reaches here.
    constexpr int TOOLCHANGER = 2;
    return resolve_op_button_slot(info, tool_index, TOOLCHANGER);
}

// ============================================================================
// Construction / Destruction
// ============================================================================

AmsToolheadMenu::AmsToolheadMenu() {
    init_subjects();
    spdlog::debug("[AmsToolheadMenu] Constructed");
}

void AmsToolheadMenu::init_subjects() {
    if (s_subjects_initialized_ || !lv_is_initialized()) {
        return;
    }
    lv_subject_init_int(&s_show_select_subject_, 0);
    lv_xml_register_subject(nullptr, "ams_toolhead_show_select", &s_show_select_subject_);
    lv_subject_init_int(&s_show_park_subject_, 0);
    lv_xml_register_subject(nullptr, "ams_toolhead_show_park", &s_show_park_subject_);
    lv_subject_init_int(&s_show_load_subject_, 0);
    lv_xml_register_subject(nullptr, "ams_toolhead_show_load", &s_show_load_subject_);
    lv_subject_init_int(&s_show_unload_subject_, 0);
    lv_xml_register_subject(nullptr, "ams_toolhead_show_unload", &s_show_unload_subject_);

    lv_subject_init_string(&s_title_subject_, s_title_buf_, nullptr, sizeof(s_title_buf_), "");
    lv_xml_register_subject(nullptr, "ams_toolhead_title", &s_title_subject_);

    s_subjects_initialized_ = true;
    // Torn down with every other static subject: after the panels (and so every
    // card bound to these) are gone, before lv_deinit().
    StaticSubjectRegistry::instance().register_deinit("AmsToolheadMenu", deinit_subjects);
}

void AmsToolheadMenu::deinit_subjects() {
    if (!s_subjects_initialized_) {
        return;
    }
    lv_subject_deinit(&s_show_select_subject_);
    lv_subject_deinit(&s_show_park_subject_);
    lv_subject_deinit(&s_show_load_subject_);
    lv_subject_deinit(&s_show_unload_subject_);
    lv_subject_deinit(&s_title_subject_);
    s_subjects_initialized_ = false;
}

AmsToolheadMenu::~AmsToolheadMenu() {
    // The subjects are the class's, not this instance's, and outlive it. Only
    // the widget tree comes down here (the base would hide() too; doing it in
    // this body keeps the order explicit).
    hide();
    spdlog::trace("[AmsToolheadMenu] Destroyed");
}

// ============================================================================
// Public API
// ============================================================================

void AmsToolheadMenu::set_action_callback(ActionCallback callback) {
    action_callback_ = std::move(callback);
}

void AmsToolheadMenu::publish_model() {
    if (!s_subjects_initialized_) {
        return;
    }
    lv_subject_set_int(&s_show_select_subject_, model_.show_select ? 1 : 0);
    lv_subject_set_int(&s_show_park_subject_, model_.show_park ? 1 : 0);
    lv_subject_set_int(&s_show_load_subject_, model_.show_load ? 1 : 0);
    lv_subject_set_int(&s_show_unload_subject_, model_.show_unload ? 1 : 0);

    snprintf(s_title_buf_, sizeof(s_title_buf_), "%s T%d", lv_tr("Toolhead"),
             tool_index_ >= 0 ? tool_index_ : 0);
    lv_subject_copy_string(&s_title_subject_, s_title_buf_);
}

bool AmsToolheadMenu::show_at(lv_obj_t* parent, lv_obj_t* anchor, lv_point_t click_pt,
                              int tool_index, AmsBackend* backend) {
    register_callbacks();
    init_subjects();

    // A toolchanger surface only. Every entry is a carriage or per-head
    // operation, and on a hub/selector backend `mounted_tool` never exists (the
    // rule would then offer Select for every head -- selector motion on Happy
    // Hare, a full feed on an ACE, a refusal on CFS/AFC). See the header.
    if (backend && backend->get_topology() != PathTopology::PARALLEL) {
        spdlog::debug("[AmsToolheadMenu] Not a toolchanger topology - no toolhead menu for T{}",
                      tool_index);
        return false;
    }

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
        slot_index = toolhead_slot_for_tool(info, tool_index);
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
    set_click_point(click_pt);

    // Base class handles: XML creation, on_created callback, positioning, and
    // claiming the active-menu slot the static callbacks resolve through.
    const bool result = show_near_widget(parent, tool_index, anchor);

    spdlog::debug("[AmsToolheadMenu] Shown for T{} (slot {}; select={} park={} load={} unload={})",
                  tool_index, slot_index, model_.show_select, model_.show_park, model_.show_load,
                  model_.show_unload);
    return result;
}

// ============================================================================
// ContextMenu overrides
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

void AmsToolheadMenu::on_backdrop_clicked() {
    spdlog::debug("[AmsToolheadMenu] Backdrop clicked");
    dispatch_toolhead_action(ToolheadAction::CANCELLED);
}

// ============================================================================
// Event Handlers
// ============================================================================

void AmsToolheadMenu::dispatch_toolhead_action(ToolheadAction action) {
    ActionCallback callback_copy = action_callback_;
    const int tool = tool_index_;
    const int slot = slot_index_;

    hide();

    if (callback_copy) {
        callback_copy(action, tool, slot);
    }
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

    // The backdrop's callback is the shared context_menu_backdrop_cb, owned by
    // ContextMenu; only the entries are this menu's own.
    register_xml_callbacks({
        {"ams_toolhead_select_cb", on_select_cb},
        {"ams_toolhead_park_cb", on_park_cb},
        {"ams_toolhead_load_cb", on_load_cb},
        {"ams_toolhead_unload_cb", on_unload_cb},
    });

    callbacks_registered_ = true;
    spdlog::debug("[AmsToolheadMenu] Callbacks registered");
}

// ============================================================================
// Static Callbacks (Instance Lookup via ContextMenu::active())
// ============================================================================

AmsToolheadMenu* AmsToolheadMenu::get_active_instance() {
    auto* self = ContextMenu::active_as<AmsToolheadMenu>();
    if (!self) {
        spdlog::warn("[AmsToolheadMenu] No active instance for event");
    }
    return self;
}

void AmsToolheadMenu::on_select_cb(lv_event_t* /*e*/) {
    if (auto* self = get_active_instance()) {
        self->handle_select();
    }
}

void AmsToolheadMenu::on_park_cb(lv_event_t* /*e*/) {
    if (auto* self = get_active_instance()) {
        self->handle_park();
    }
}

void AmsToolheadMenu::on_load_cb(lv_event_t* /*e*/) {
    if (auto* self = get_active_instance()) {
        self->handle_load();
    }
}

void AmsToolheadMenu::on_unload_cb(lv_event_t* /*e*/) {
    if (auto* self = get_active_instance()) {
        self->handle_unload();
    }
}

// ============================================================================
// Shared wiring — the panels call these
// ============================================================================

void dispatch_toolhead_menu_action(AmsToolheadMenu::ToolheadAction action, int tool_index,
                                   int slot_index) {
    using TA = AmsToolheadMenu::ToolheadAction;
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        return;
    }

    AmsError err{};
    switch (action) {
    case TA::CANCELLED:
        return;
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
    }

    if (err.result != AmsResult::SUCCESS) {
        spdlog::warn("[AmsToolheadMenu] T{} (slot {}) action failed: {}", tool_index, slot_index,
                     err.technical_msg);
        notify_ams_error(err, lv_tr("Toolhead command failed"));
    }
}

bool show_toolhead_menu_at_touch(std::unique_ptr<AmsToolheadMenu>& menu, lv_obj_t* parent_screen,
                                 lv_obj_t* canvas, int tool_index) {
    if (!canvas) {
        return false;
    }
    // In range before it reaches get_slot_info(): the canvas hands back a tool
    // number, and every backend call the menu makes is slot-indexed.
    const int slot_count = lv_subject_get_int(AmsState::instance().get_slot_count_subject());
    if (tool_index < 0 || tool_index >= slot_count) {
        spdlog::warn("[AmsToolheadMenu] Ignoring toolhead click - invalid tool {} (slot_count={})",
                     tool_index, slot_count);
        return false;
    }
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        return false;
    }

    // Live touch point, read synchronously while the indev still reports the
    // press coordinates, so the menu opens where the finger is.
    lv_point_t click_pt = {0, 0};
    if (lv_indev_t* indev = lv_indev_active()) {
        lv_indev_get_point(indev, &click_pt);
    }

    // Created once per panel and reused: a menu keeps its widget tree between
    // shows. The dispatch is the shared one -- pure backend work.
    if (!menu) {
        menu = std::make_unique<AmsToolheadMenu>();
        menu->set_action_callback(dispatch_toolhead_menu_action);
    }
    // false when the head has no applicable action, or the backend is not a
    // toolchanger: a deliberate no-op, not an error worth reporting.
    return menu->show_at(parent_screen, canvas, click_pt, tool_index, backend);
}

} // namespace helix::ui
