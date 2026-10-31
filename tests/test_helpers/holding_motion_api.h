// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "axis_move.h"
#include "moonraker_api.h"
#include "moonraker_motion_api.h"

#include <optional>
#include <utility>
#include <vector>

namespace helix {

/// Holds every move instead of acking it, so a test decides when (and whether) each one
/// lands: the mock client acks synchronously, which keeps a coalescer from ever holding
/// anything pending.
class HoldingMotionAPI : public MoonrakerMotionAPI {
  public:
    using MoonrakerMotionAPI::MoonrakerMotionAPI;
    struct Held {
        AxisTarget target;
        SuccessCallback on_success;
        ErrorCallback on_error;
        std::optional<double> start_z;
    };
    struct HeldRelative {
        double dx;
        double dy;
        double dz;
        SuccessCallback on_success;
        ErrorCallback on_error;
    };
    void move_to(const AxisTarget& target, double, double, SuccessCallback on_success,
                 ErrorCallback on_error, std::optional<double> start_z) override {
        held.push_back({target, std::move(on_success), std::move(on_error), start_z});
    }
    void move_relative(double dx, double dy, double dz, double, double, SuccessCallback on_success,
                       ErrorCallback on_error) override {
        held_relative.push_back({dx, dy, dz, std::move(on_success), std::move(on_error)});
    }
    std::vector<Held> held;
    std::vector<HeldRelative> held_relative;
};

/// A MoonrakerAPI whose motion() is a HoldingMotionAPI.
class HoldingAPI : public MoonrakerAPI {
  public:
    HoldingAPI(IMoonrakerClient& client, PrinterState& state)
        : MoonrakerAPI(client, state), holding_(client, state, get_safety_limits()) {}
    MoonrakerMotionAPI& motion() override {
        return holding_;
    }
    HoldingMotionAPI holding_;
};

} // namespace helix
