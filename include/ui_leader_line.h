// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @brief Register the <leader_line> XML widget: an lv_line.
 *
 * XML declares the line, its style and its visibility bindings; the owner sets
 * its points from measured layout. lv_line keeps the points pointer, so the
 * owner must keep that array alive while the line exists.
 *
 * LVGL dashes only horizontal and vertical lines, and a leader's first leg
 * runs at 45 degrees, so a line styled with line_dash_width / line_dash_gap
 * draws its own dashes along every leg, the pattern running on across the
 * elbow. Without a dash style it draws as a plain lv_line.
 */

#include <cmath>
#include <cstddef>
#include <vector>

namespace helix::ui {

void register_leader_line_widget();

struct LeaderPoint {
    float x = 0;
    float y = 0;
};
struct LeaderDash {
    LeaderPoint a;
    LeaderPoint b;
};

/// The dashes along a polyline: `dash` px drawn, `gap` px skipped, the
/// pattern carried from one leg into the next. Empty for fewer than two
/// points or a non-positive dash. At most kMaxLeaderDashSteps dashes and gaps
/// in all: this runs on every draw, so no input may make it unbounded.
constexpr size_t kMaxLeaderDashSteps = 8192;

inline std::vector<LeaderDash> leader_dashes(const LeaderPoint* pts, size_t count, float dash,
                                             float gap) {
    std::vector<LeaderDash> out;
    if (!pts || count < 2 || !(dash > 0) || !std::isfinite(dash))
        return out;
    gap = gap > 0 && std::isfinite(gap) ? gap : 0;
    const float period = dash + gap;
    float phase = 0; // distance into the current period at a leg's start
    size_t steps = 0;
    for (size_t i = 0; i + 1 < count; ++i) {
        const float dx = pts[i + 1].x - pts[i].x;
        const float dy = pts[i + 1].y - pts[i].y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (!(len > 0) || !std::isfinite(len))
            continue;
        const auto at = [&](float t) {
            return LeaderPoint{pts[i].x + dx * t / len, pts[i].y + dy * t / len};
        };
        float t = 0;
        while (t < len && steps++ < kMaxLeaderDashSteps) {
            const bool drawing = phase < dash;
            const float end = std::fmin(len, t + ((drawing ? dash : period) - phase));
            if (end <= t) {
                // What is left of this dash or gap is too small to move t:
                // count it as done.
                phase = drawing ? dash : 0;
                continue;
            }
            if (drawing)
                out.push_back({at(t), at(end)});
            phase += end - t;
            t = end;
            if (phase >= period)
                phase = 0;
        }
    }
    return out;
}

} // namespace helix::ui
