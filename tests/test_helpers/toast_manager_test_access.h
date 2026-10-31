// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_toast_manager.h"

#include "lvgl.h"

#include <iterator>

// Friend accessor (L065): reach ToastManager privates without test-only
// methods on the production class. One definition for every toast test file
// - a second copy in any test .cpp trips the one-TestAccess-per-name lint.

class ToastManagerTestAccess {
  public:
    static void inject(ToastManager& tm, ToastSeverity sev, const char* msg, bool exiting) {
        ToastManager::ToastInstance inst;
        inst.severity = sev;
        inst.message = msg;
        inst.is_exiting = exiting;
        tm.active_.push_back(std::move(inst));
    }
    static void inject_widget(ToastManager& tm, lv_obj_t* widget, ToastSeverity sev,
                              const char* msg) {
        ToastManager::ToastInstance inst;
        inst.widget = widget;
        inst.severity = sev;
        inst.message = msg;
        tm.active_.push_back(std::move(inst));
    }
    static void inject_action(ToastManager& tm, ToastSeverity sev, const char* msg,
                              toast_action_callback_t cb, void* user_data) {
        ToastManager::ToastInstance inst;
        inst.severity = sev;
        inst.message = msg;
        inst.action_cb = cb;
        inst.action_user_data = user_data;
        tm.active_.push_back(std::move(inst));
    }
    /// Stage a closeable toast: an action identity plus the user-closed
    /// callback, on a widget the removal paths can resolve.
    static void inject_closeable(ToastManager& tm, lv_obj_t* widget,
                                 toast_action_callback_t action_cb, void* user_data,
                                 toast_action_callback_t user_closed_cb);
    static bool refresh_action_duplicate(ToastManager& tm, ToastSeverity sev, const char* msg,
                                         toast_action_callback_t cb, void* user_data) {
        return tm.refresh_duplicate(sev, msg, cb, user_data);
    }
    static ToastManager::ToastList::iterator find_owning_toast(ToastManager& tm, lv_obj_t* node) {
        return tm.find_owning_toast(node);
    }
    static ToastManager::ToastList::iterator list_end(ToastManager& tm) {
        return tm.active_.end();
    }
    static bool refresh_duplicate(ToastManager& tm, ToastSeverity sev, const char* msg) {
        return tm.refresh_duplicate(sev, msg);
    }
    static void user_close(ToastManager& tm, lv_obj_t* widget) {
        tm.user_close_toast(tm.find_by_widget(widget));
    }
    static void clear(ToastManager& tm) {
        tm.active_.clear();
    }
};

inline void ToastManagerTestAccess::inject_closeable(ToastManager& tm, lv_obj_t* widget,
                                                     toast_action_callback_t action_cb,
                                                     void* user_data,
                                                     toast_action_callback_t user_closed_cb) {
    // detach_from_input() requires the action button by name; without it the
    // strict-mode UI contract aborts before the path under test runs.
    lv_obj_t* action_btn = lv_obj_create(widget);
    lv_obj_set_name(action_btn, "toast_action_btn");
    tm.active_.push_back(ToastManager::ToastInstance{});
    auto it = std::prev(tm.active_.end());
    it->widget = widget;
    it->action_cb = action_cb;
    it->action_user_data = user_data;
    it->user_closed_cb = user_closed_cb;
}
