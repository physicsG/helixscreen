// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file scoped_pointer_indev.h
 * @brief A synthetic pointer a test drives one read at a time.
 *
 * What one read does (lib/lvgl/src/indev/lv_indev.c#lv_indev_read), which the
 * helpers below are built around:
 *
 * - A read dispatches synchronously, and lv_indev_active() is non-null only
 *   while it runs. A handler that asks the active indev for the pointer
 *   (edit-mode selection, drag, long press) runs for real only from inside a
 *   read; lv_obj_send_event() reaches it with no indev.
 * - A read advances no time. Event timestamps and the long-press clock read
 *   lv_tick_get(), and only lv_tick_inc() moves it; hold() does.
 * - The first PRESSED read after a RELEASED one dispatches PRESSED and then
 *   PRESSING to the object under the point, both inside that one call. Each
 *   further PRESSED read dispatches PRESSING, and, from inside the first read
 *   that finds long_press_ms() elapsed since the press, LONG_PRESSED, unless a
 *   scroll has taken the gesture.
 * - A RELEASED read dispatches RELEASED to the pressed object, then CLICKED
 *   when that object received the press's PRESSED and no scroll took the
 *   gesture.
 * - lv_indev_reset() forgets the pressed object. A RELEASED read after it
 *   dispatches nothing. A PRESSED read after it finds the object under the
 *   point again and, the finger having stayed down, dispatches PRESSING
 *   without PRESSED.
 * - Nothing reads on its own: the indev's read timer is periodic, and
 *   lv_timer_handler_safe() fires only one-shot timers.
 */

#pragma once

#include "../../lib/lvgl/src/indev/lv_indev_private.h"
#include "../ui_test_utils.h"
#include "lvgl/lvgl.h"

#include <cstdint>
#include <functional>

namespace helix_test {

/// Owns one pointer indev for a scope. The point and state it reports live in
/// the instance, reached from the read callback through the indev's user data,
/// so helpers are independent of each other and the header holds no shared
/// state.
class ScopedPointerIndev {
  public:
    /// One pointer read period on the device: LVGL's indev read timer runs at
    /// the display refresh period.
    static constexpr uint32_t READ_PERIOD_MS = LV_DEF_REFR_PERIOD;

    ScopedPointerIndev() {
        indev_ = lv_indev_create();
        lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev_, read_cb);
        lv_indev_set_user_data(indev_, this);
    }

    /// Deleted at scope exit, so no later lv_timer_handler() call can run the
    /// indev's read timer against this instance.
    ~ScopedPointerIndev() {
        lv_indev_delete(indev_);
    }

    ScopedPointerIndev(const ScopedPointerIndev&) = delete;
    ScopedPointerIndev& operator=(const ScopedPointerIndev&) = delete;

    lv_indev_t* indev() const {
        return indev_;
    }

    /// The indev's long-press time, in ms.
    uint32_t long_press_ms() const {
        return indev_->long_press_time;
    }

    /// Hold length after press() that ends on the read dispatching
    /// LONG_PRESSED: hold() reads every READ_PERIOD_MS starting at the press's
    /// own tick, so its last read is the first at or past long_press_ms().
    uint32_t long_press_hold_ms() const {
        return long_press_ms() + READ_PERIOD_MS;
    }

    /// One read reporting @p state at (@p x, @p y).
    void send(int x, int y, lv_indev_state_t state) {
        point_ = {x, y};
        state_ = state;
        lv_indev_read(indev_);
    }

    /// A PRESSED read. From RELEASED it dispatches PRESSED, then PRESSING.
    void press(int x, int y) {
        send(x, y, LV_INDEV_STATE_PRESSED);
    }

    /// A PRESSED read at a new point while the finger stays down: PRESSING.
    void move(int x, int y) {
        send(x, y, LV_INDEV_STATE_PRESSED);
    }

    /// A RELEASED read.
    void release(int x, int y) {
        send(x, y, LV_INDEV_STATE_RELEASED);
    }

    /// A press held for one more read at the same point: PRESSED and PRESSING,
    /// then a second PRESSING.
    void prime(int x, int y) {
        press(x, y);
        press(x, y);
    }

    /// The two-step grab: a press that selects the widget under the point, the
    /// lift, then a fresh press on the now-selected widget, whose first
    /// PRESSING arms it. Returns with that press still down.
    void grab(int x, int y) {
        prime(x, y);
        release(x, y);
        prime(x, y);
    }

    /// Hold the pointer down at the last point sent for @p ms at the device's
    /// read cadence: a PRESSED read, then one read period of virtual time with
    /// one-shot timers and animations stepped, so a page slide advances between
    /// reads as it does on the device. @p after_cycle runs at the end of every
    /// cycle.
    void hold(uint32_t ms, const std::function<void()>& after_cycle = nullptr) {
        for (uint32_t held = 0; held < ms; held += READ_PERIOD_MS) {
            press(point_.x, point_.y);
            lv_tick_inc(READ_PERIOD_MS);
            lv_timer_handler_safe();
            if (after_cycle) {
                after_cycle();
            }
        }
    }

  private:
    static void read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
        auto* self = static_cast<ScopedPointerIndev*>(lv_indev_get_user_data(indev));
        data->point = self->point_;
        data->state = self->state_;
    }

    lv_indev_t* indev_ = nullptr;
    lv_point_t point_{0, 0};
    lv_indev_state_t state_{LV_INDEV_STATE_RELEASED};
};

} // namespace helix_test
