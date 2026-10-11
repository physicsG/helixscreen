// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ams_types.h"

#include <array>
#include <chrono>
#include <mutex>

namespace helix {

/**
 * @brief Credits a runout-sensor edge to an unload the user just asked for.
 *
 * Two windows share one duration. The post-unload grace is a one-shot armed
 * when an operation that passed through UNLOADING returns to IDLE; the per-slot
 * stamps record a lane's unload_finish. Either way, past WINDOW an empty sensor
 * is a real runout again.
 *
 * Read from the WebSocket thread (FilamentSensorManager) and written from both
 * the main thread (AmsState::sync_from_backend) and backend status handlers, so
 * every member sits behind a leaf mutex: nothing here calls out while holding it.
 */
class RunoutGrace {
  public:
    using Clock = std::chrono::steady_clock;

    /// Same 30s as the AD5X IFS runout suppression.
    static constexpr std::chrono::seconds WINDOW{30};
    /// One stamp per AmsState slot subject.
    static constexpr int MAX_SLOTS = 16;

    /// Feed one action sample. Arms the one-shot when an operation that passed
    /// through UNLOADING ends; tracked across the whole operation because a
    /// backend may report sub-phases (CUTTING, ...) after the UNLOADING it began with.
    void on_action(AmsAction prev, AmsAction now);

    /// Filament is back at the toolhead: the removal the grace was armed for
    /// has happened or been superseded.
    void on_filament_loaded();

    /// Take the one-shot. True at most once per unload, and only inside WINDOW.
    [[nodiscard]] bool consume();

    /// Is the one-shot armed and inside WINDOW, without spending it.
    [[nodiscard]] bool armed() const;

    void mark_slot_unloaded(int slot_index);
    [[nodiscard]] bool was_slot_recently_unloaded(int slot_index) const;

    /// Forget everything; the state describes a backend that is going away.
    void reset();

  private:
    friend class AmsStateTestAccess;

    mutable std::mutex mutex_;
    bool armed_{false};
    /// Without a stamp the flag has no time bound, and an unload that leaves
    /// nothing loaded never reaches on_filament_loaded(): the next genuine idle
    /// runout, days later, would be swallowed.
    Clock::time_point armed_at_{};
    bool saw_unload_in_op_{false};
    /// time_point{} means never unloaded and is always outside WINDOW.
    std::array<Clock::time_point, MAX_SLOTS> last_unload_{};
};

} // namespace helix
