// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Route-geometry checks shared by the route plan tests.

#include "filament_path_geometry.h"

#include <cmath>

namespace fpath_test {

namespace pg = helix::ui::pathgeo;

inline pg::PathPoint seg_start(const pg::PathSeg& s) {
    if (s.type == pg::PathSeg::LINE)
        return s.p0;
    return {s.center.x + s.radius * std::cos(s.start_angle),
            s.center.y + s.radius * std::sin(s.start_angle)};
}

inline pg::PathPoint seg_end(const pg::PathSeg& s) {
    if (s.type == pg::PathSeg::LINE)
        return s.p1;
    const float a = s.start_angle + s.sweep;
    return {s.center.x + s.radius * std::cos(a), s.center.y + s.radius * std::sin(a)};
}

inline bool near(pg::PathPoint p, float x, float y, float eps = 0.01f) {
    return std::fabs(p.x - x) <= eps && std::fabs(p.y - y) <= eps;
}

/// Every segment starts where the previous one ended: no gap anywhere.
inline bool contiguous(const pg::FilamentPath& p) {
    for (int i = 1; i < p.count; i++) {
        const pg::PathPoint a = seg_end(p.segs[i - 1]);
        if (!near(seg_start(p.segs[i]), a.x, a.y))
            return false;
    }
    return true;
}

} // namespace fpath_test
