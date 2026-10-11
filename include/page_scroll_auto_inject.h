// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl/lvgl.h"
#include "page_scroll_controller.h"

#include <cstddef>
#include <memory>
#include <unordered_map>

namespace helix::ui {

/** Global policy that, when the display setting is on, walks each just-shown
 *  panel/overlay root and attaches a PageScrollController to every overflowing,
 *  vertically-scrollable, non-nested container. Main-thread only. */
class PageScrollAutoInject {
  public:
    static PageScrollAutoInject& instance();

    void init();     ///< lifecycle hook (idempotent); see .cpp for why no observer
    void shutdown(); ///< detach all controllers
    void on_root_shown(lv_obj_t* root);
    /// Apply a runtime change to the setting (called from the Display-settings
    /// toggle callback): inject into the current screen when enabled, else detach all.
    void on_setting_toggled(bool enabled);
    void detach_all();
    void prune_dead(); ///< erase controllers whose container died

    /// Live setting value (no cache) — see on_root_shown() for why this class
    /// never trusts a stale copy of the observed value.
    bool enabled() const;
    std::size_t managed_count() const {
        return controllers_.size();
    }
    /// Re-walks triggered by a watched container overflowing after its walk.
    std::size_t late_rewalk_count() const {
        return late_rewalks_;
    }

  private:
    PageScrollAutoInject() = default;
    void walk_and_attach(lv_obj_t* obj, bool ancestor_managed);
    static bool qualifies(lv_obj_t* obj);
    static bool may_overflow_later(lv_obj_t* obj);
    static void late_fill_cb(lv_event_t* e);

    bool rewalk_pending_ = false;
    std::size_t late_rewalks_ = 0;

    std::unordered_map<lv_obj_t*, std::unique_ptr<PageScrollController>> controllers_;
};

} // namespace helix::ui
