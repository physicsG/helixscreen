// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_runout_grace.h"

#include <spdlog/spdlog.h>

namespace helix {

void RunoutGrace::on_action(AmsAction prev, AmsAction now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (now == AmsAction::UNLOADING) {
        saw_unload_in_op_ = true;
    }
    if (now == AmsAction::IDLE && prev != AmsAction::IDLE) {
        armed_ = saw_unload_in_op_;
        if (armed_) {
            armed_at_ = Clock::now();
        }
        saw_unload_in_op_ = false;
    }
}

void RunoutGrace::on_filament_loaded() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (armed_) {
        armed_ = false;
        spdlog::debug("[AmsState] Post-unload runout grace retired — filament loaded again");
    }
}

bool RunoutGrace::consume() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!armed_) {
        return false;
    }
    armed_ = false;
    const auto age = Clock::now() - armed_at_;
    if (age >= WINDOW) {
        spdlog::debug("[AmsState] Post-unload runout grace expired unused after {}s",
                      std::chrono::duration_cast<std::chrono::seconds>(age).count());
        return false;
    }
    return true;
}

bool RunoutGrace::armed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // Does NOT clear on expiry: only the consumer spends the shot, so a peek
    // that also disarmed would be a second consumer by another name.
    return armed_ && (Clock::now() - armed_at_) < WINDOW;
}

void RunoutGrace::mark_slot_unloaded(int slot_index) {
    if (slot_index < 0 || slot_index >= MAX_SLOTS) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    last_unload_[slot_index] = Clock::now();
    spdlog::debug("[AmsState] marked slot {} as recently unloaded (runout grace started)",
                  slot_index);
}

bool RunoutGrace::was_slot_recently_unloaded(int slot_index) const {
    if (slot_index < 0 || slot_index >= MAX_SLOTS) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto t = last_unload_[slot_index];
    if (t.time_since_epoch().count() == 0) {
        return false;
    }
    return (Clock::now() - t) < WINDOW;
}

void RunoutGrace::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    armed_ = false;
    armed_at_ = {};
    saw_unload_in_op_ = false;
    last_unload_ = {};
}

} // namespace helix
