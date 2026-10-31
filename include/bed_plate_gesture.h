// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The press / drag / release decision for a top-down plate surface, shared by the Motion
// panel's Bed tab and the home Toolhead widget.
//
// Free of LVGL so it is unit-testable. The owner turns input events into calls here and
// acts on what comes back: a drag streams targets through the owner's send hook with the
// gesture's single lift decision stamped on each; a tap is handed back for the owner to
// treat its own way (the Bed tab moves at once, the widget shows a target to confirm).

#include "axis_move.h"
#include "motion_presets.h"

#include <functional>
#include <optional>

namespace helix {

/// One pointer event on a plate's touch surface.
enum class BedTouch { Pressed, Pressing, Released, Lost };

/// Movement, in pixels on either axis, before a press counts as a drag.
inline constexpr int kPlateDragThresholdPx = 8;

class BedPlateGesture {
  public:
    struct Hooks {
        /// Map a point (owner's coordinates) to a plate target; nullopt off the plate or
        /// with unknown bounds.
        std::function<std::optional<AxisTarget>(int x, int y)> target_at;
        /// Whether the plate may move the head right now (homed, idle, not blocked).
        std::function<bool()> can_move;
        /// The lift for the gesture's first move; asked once per gesture.
        std::function<PlateLift()> lift;
        /// Send one target with the gesture's lift applied (z set when the lift is Lift).
        std::function<void(const AxisTarget&)> send;
        /// The gesture's lift came back Refuse: the owner tells the user why.
        std::function<void()> refused;
    };

    explicit BedPlateGesture(Hooks hooks);

    /// Returns false (and ignores the rest of the gesture) when can_move() refuses.
    bool press(int x, int y);
    /// Streams the target under the finger once the drag threshold is crossed. A sample
    /// mapping to the target already sent sends nothing. Every send asks can_move() again,
    /// and a refusal ends the stream for the rest of the gesture. Returns whether the drag
    /// target changed, which is when an owner's readout of it needs redrawing.
    bool pressing(int x, int y);

    struct Release {
        bool was_drag = false;
        std::optional<AxisTarget> tap_target; ///< set for a tap on the plate; never for a drag
    };
    /// Ends the gesture. A drag sends its release point if that point was not already sent.
    Release release(int x, int y);
    /// The press was lost (scroll took over, widget deleted): end without sending.
    void lost();

    /// Send one target outside a drag (the Bed tab's tap, the widget's Move here, a
    /// preset): asks can_move() and lift() afresh, calls refused() on Refuse. Returns
    /// whether a target was sent.
    bool send_once(const AxisTarget& target);

    /// A send for this gesture failed: stop streaming until the next press.
    void mark_failed();

    [[nodiscard]] bool active() const {
        return pressing_;
    }
    [[nodiscard]] bool dragging() const {
        return dragging_;
    }
    /// The target under the finger during a drag, for the owner's readout.
    [[nodiscard]] const std::optional<AxisTarget>& drag_target() const {
        return drag_target_;
    }

  private:
    void send_target(const AxisTarget& target);

    Hooks hooks_;
    bool pressing_ = false;
    bool dragging_ = false;
    bool failed_ = false;
    int press_x_ = 0;
    int press_y_ = 0;
    std::optional<AxisTarget> drag_target_;
    std::optional<AxisTarget> sent_target_;
    std::optional<PlateLift> lift_;
};

} // namespace helix
