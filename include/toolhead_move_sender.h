// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Sends jog deltas and absolute targets with one move in flight and the newest request
// coalesced behind it (JogCoalescer), clamped to the G-code envelope and at the user's jog
// feedrates. MotionPanel and the home Toolhead widget each own one: the two never move the
// head at the same time, since the Motion overlay covers the home panel.
//
// Main thread only. The owner's lifetime guard wraps every RPC callback, so an owner torn
// down mid-move is never called back.

#include "async_lifetime_guard.h"
#include "axis_move.h"
#include "jog_coalescer.h"
#include "moonraker_error.h"
#include "printer_motion_state.h"

#include <functional>
#include <string>

namespace helix {

class IMoonrakerAPI;

/// A Klipper error made fit for a toast: prefixes and firmware JSON envelopes stripped, a
/// homing refusal reworded, and long text truncated.
[[nodiscard]] std::string clean_gcode_error(const std::string& msg);

class ToolheadMoveSender {
  public:
    struct Hooks {
        /// After every ack, once any pending flush is sent.
        std::function<void()> on_ack;
        /// After the coalescer is reset for a refused move; the sender has already
        /// raised the "Jog failed" notification.
        std::function<void(const MoonrakerError&)> on_error;
        /// Asked on every ack with the move coalesced behind it, before it is sent. False
        /// drops that move and resets the coalescer: the owner's gate for that kind of move
        /// closed while the head was moving. Unset means always send.
        std::function<bool(const JogCoalescer::CoalescedMove&)> may_flush;
    };

    /// @p tag names the owner in logs and in the lifetime guard's callback tags.
    ToolheadMoveSender(AsyncLifetimeGuard& lifetime, const char* tag, Hooks hooks);

    /// Send or coalesce an absolute target, clamped to @p clamp_bounds. Returns false
    /// (with an "Axis limits unknown" notice) when an axis the target sets has no bounds,
    /// or when there is no API. @p current_z is the reported Z the lift is measured from.
    bool send_target(IMoonrakerAPI* api, const AxisTarget& target, const AxisBounds& clamp_bounds,
                     double current_z);

    /// Send or coalesce a relative jog. Returns false when there is no API.
    bool send_delta(IMoonrakerAPI* api, const AxisMove& delta);

    /// Where the next target starts in Z: the in-flight move's Z when it set or changed
    /// one, @p current_z otherwise. Pending travel does not count: a new target replaces it.
    [[nodiscard]] double target_start_z(double current_z) const;

    [[nodiscard]] const JogCoalescer& coalescer() const {
        return coalescer_;
    }

    /// Drop everything queued (deactivate, print start, teardown).
    void reset();

  private:
    bool send(IMoonrakerAPI* api, const JogCoalescer::CoalescedMove& move);

    AsyncLifetimeGuard& lifetime_;
    const char* tag_;
    Hooks hooks_;
    JogCoalescer coalescer_;
    double target_start_z_ = 0.0;
};

} // namespace helix
