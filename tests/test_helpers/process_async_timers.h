// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "misc/lv_timer_private.h"

/// True while @p timer is still in LVGL's timer list.
inline bool timer_is_live(const lv_timer_t* timer) {
    for (lv_timer_t* t = lv_timer_get_next(nullptr); t; t = lv_timer_get_next(t)) {
        if (t == timer) {
            return true;
        }
    }
    return false;
}

/// Process pending lv_async_call / lv_obj_delete_async one-shot timers.
/// Unlike lv_timer_handler(), this only fires one-shot timers and avoids the
/// infinite-loop problem with display refresh timers in the test fixture:
/// lv_timer_handler() loops on them, so tests must drive one-shots by hand.
/// Restarts from the head of the timer list after each fire — callbacks may
/// have modified the list.
///
/// Each fire spends a repeat the way lv_timer_exec() does, and a timer whose
/// count is spent is deleted unless its callback already deleted it. A timer
/// left armed would fire again on the next pass, and a callback that frees its
/// user data on the first fire reads freed memory on the second.
inline void process_async_timers() {
    for (int safety = 0; safety < 100; safety++) {
        bool fired = false;
        lv_timer_t* t = lv_timer_get_next(nullptr);
        while (t) {
            lv_timer_t* next = lv_timer_get_next(t);
            if (t->repeat_count > 0 && t->timer_cb) {
                t->repeat_count--;
                t->timer_cb(t);
                if (timer_is_live(t) && t->repeat_count == 0) {
                    lv_timer_delete(t);
                }
                fired = true;
                break; // Restart — list may have changed
            }
            t = next;
        }
        if (!fired)
            break;
    }
}
