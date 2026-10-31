// SPDX-License-Identifier: GPL-3.0-or-later
#include "belt_settle.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace helix::calibration {

namespace {

float inset_for(float span, int tool_count) {
    if (tool_count > 1) {
        return belt_settle::kMultiToolInsetSpanFraction * span;
    }
    return std::max(belt_settle::kInsetMinMm, belt_settle::kInsetSpanFraction * span);
}

} // namespace

BeltSettlePlan plan_belt_settle(const BeltSettleInputs& in) {
    namespace bs = belt_settle;
    BeltSettlePlan plan;

    if (!in.has_xy_limits) {
        plan.refusal = BeltSettleRefusal::NO_AXIS_LIMITS;
        return plan;
    }
    if (!in.probe_point) {
        plan.refusal = BeltSettleRefusal::NO_PROBE_POINT;
        return plan;
    }
    if (!(in.max_velocity_mm_s > 0.0f)) {
        plan.refusal = BeltSettleRefusal::NO_VELOCITY;
        return plan;
    }

    const float inset_x = inset_for(in.x_max - in.x_min, in.tool_count);
    const float inset_y = inset_for(in.y_max - in.y_min, in.tool_count);
    // Machine-space rectangle corners.
    const float x0 = in.x_min + inset_x;
    const float x1 = in.x_max - inset_x;
    const float y0 = in.y_min + inset_y;
    const float y1 = in.y_max - inset_y;
    const float side_x = x1 - x0;
    const float side_y = y1 - y0;
    if (side_x < bs::kMinSideMm || side_y < bs::kMinSideMm) {
        plan.refusal = BeltSettleRefusal::SPAN_TOO_SMALL;
        return plan;
    }

    const auto& probe = *in.probe_point;
    const float speed =
        std::min(bs::kSpeedMaxMmS, bs::kSpeedVelocityFraction * in.max_velocity_mm_s);
    const float feed = speed * 60.0f;

    // G-code coordinates are machine minus the homing origin.
    const float gx0 = x0 - in.origin_x;
    const float gx1 = x1 - in.origin_x;
    const float gy0 = y0 - in.origin_y;
    const float gy1 = y1 - in.origin_y;

    std::vector<std::string> lines;
    lines.push_back(fmt::format("SAVE_GCODE_STATE NAME={}", bs::kStateName));
    lines.emplace_back("G90");
    lines.push_back(fmt::format("G1 Z{:.1f} F{:.0f}", probe[2] - in.origin_z, feed));
    lines.push_back(fmt::format("G1 X{:.1f} Y{:.1f} F{:.0f}", gx0, gy0, feed));
    for (int lap = 0; lap < bs::kLaps; ++lap) {
        lines.push_back(fmt::format("G1 X{:.1f} Y{:.1f}", gx1, gy0));
        lines.push_back(fmt::format("G1 X{:.1f} Y{:.1f}", gx1, gy1));
        lines.push_back(fmt::format("G1 X{:.1f} Y{:.1f}", gx0, gy1));
        lines.push_back(fmt::format("G1 X{:.1f} Y{:.1f}", gx0, gy0));
    }
    lines.push_back(
        fmt::format("G1 X{:.1f} Y{:.1f}", probe[0] - in.origin_x, probe[1] - in.origin_y));
    lines.emplace_back("M400");
    lines.push_back(fmt::format("RESTORE_GCODE_STATE NAME={}", bs::kStateName));

    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            plan.gcode += '\n';
        }
        plan.gcode += lines[i];
    }

    const float lap_mm = 2.0f * (side_x + side_y);
    const float return_mm = std::hypot(probe[0] - x0, probe[1] - y0);
    plan.speed_mm_s = speed;
    plan.inset_x_mm = inset_x;
    plan.inset_y_mm = inset_y;
    plan.seconds = (static_cast<float>(bs::kLaps) * lap_mm + return_mm) / speed;
    const double timeout = std::ceil(static_cast<double>(bs::kTimeoutFactor) *
                                     static_cast<double>(plan.seconds) * 1000.0);
    plan.timeout_ms = std::max(bs::kMinTimeoutMs, static_cast<uint32_t>(timeout));
    return plan;
}

} // namespace helix::calibration
