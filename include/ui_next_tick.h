// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "async_lifetime_guard.h"
#include "lvgl/lvgl.h"

#include <functional>
#include <memory>
#include <utility>

namespace helix::ui {

/// Run @p work on the next lv_timer_handler() pass, from a one-shot LVGL timer
/// (lv_async_call), outside any input dispatch and outside any UpdateQueue
/// batch: for structural UI changes an event handler or observer asks for,
/// which must not run inside that event or batch.
///
/// This form is bound to no object: it always runs. Use it for singletons and
/// for work that must happen even if its requester is gone (freeing a detached
/// controller). Anything that touches an owner's members takes the token form.
///
/// Main thread only. AsyncLifetimeGuard::defer() posts to the UpdateQueue
/// instead; this runs from the LVGL timer list.
inline void run_next_tick(std::function<void()> work) {
    auto* ctx = new std::function<void()>(std::move(work));
    lv_async_call(
        [](void* data) {
            std::unique_ptr<std::function<void()>> fn(static_cast<std::function<void()>*>(data));
            (*fn)();
        },
        ctx);
}

/// Token form: skipped when @p token has expired by then, so an owner
/// destroyed or invalidated first cancels it.
inline void run_next_tick(helix::LifetimeToken token, std::function<void()> work) {
    run_next_tick([tok = std::move(token), w = std::move(work)]() {
        if (!tok.expired()) {
            w();
        }
    });
}

} // namespace helix::ui
