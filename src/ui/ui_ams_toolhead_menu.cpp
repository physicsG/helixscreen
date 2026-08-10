// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_toolhead_menu.h"

#include "ui_button.h"
#include "ui_callback_helpers.h"

#include "ams_backend.h"
#include "ams_types.h"

#include <spdlog/spdlog.h>

#include <cstdio>

namespace helix::ui {

// Static member initialization
bool AmsToolheadMenu::callbacks_registered_ = false;
AmsToolheadMenu* AmsToolheadMenu::s_active_instance_ = nullptr;

// ============================================================================
// The rule (pure — no LVGL, no backend)
// ============================================================================

ToolheadMenuModel toolhead_menu_model(int tool_index, int mounted_tool, bool supports_park,
                                      bool slot_present, bool can_unload) {
    ToolheadMenuModel m;
    if (tool_index < 0) {
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
    m.show_load = slot_present && !can_unload;
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

AmsToolheadMenu::AmsToolheadMenu(AmsToolheadMenu&& other) noexcept
    : ContextMenu(std::move(other)), action_callback_(std::move(other.action_callback_)),
      backend_(other.backend_), tool_index_(other.tool_index_), model_(other.model_) {
    if (s_active_instance_ == &other) {
        s_active_instance_ = this;
    }
    // Subjects are not moved: they hold registered pointers into `other`'s own
    // storage, and title_buf_ is an inline array the string subject points at.
    // Re-init ours instead and let the moved-from object release its own.
    init_subjects();
    publish_model();
    other.backend_ = nullptr;
    other.tool_index_ = -1;
}

AmsToolheadMenu& AmsToolheadMenu::operator=(AmsToolheadMenu&& other) noexcept {
    if (this != &other) {
        if (s_active_instance_ == this) {
            s_active_instance_ = nullptr;
        }

        ContextMenu::operator=(std::move(other));

        action_callback_ = std::move(other.action_callback_);
        backend_ = other.backend_;
        tool_index_ = other.tool_index_;
        model_ = other.model_;

        if (s_active_instance_ == &other) {
            s_active_instance_ = this;
        }

        init_subjects();
        publish_model();

        other.backend_ = nullptr;
        other.tool_index_ = -1;
    }
    return *this;
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
    if (backend) {
        mounted = backend->get_system_info().mounted_tool;
        supports_park = backend->supports_toolhead_park();
        present = backend->get_slot_info(tool_index).is_present();
        can_unload = backend->can_unload_from_toolhead(tool_index);
    }
    model_ = toolhead_menu_model(tool_index, mounted, supports_park, present, can_unload);

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

} // namespace helix::ui
