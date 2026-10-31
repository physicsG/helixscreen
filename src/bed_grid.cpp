// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bed_grid.h"

#include <algorithm>
#include <cmath>

namespace helix {

namespace {

/// Multiples of @p spacing strictly inside (lo, hi).
std::vector<double> interior_multiples(double lo, double hi, double spacing) {
    constexpr double kEdgeEpsilon = 1e-6;
    std::vector<double> out;
    for (double v = std::ceil((lo + kEdgeEpsilon) / spacing) * spacing; v < hi - kEdgeEpsilon;
         v += spacing) {
        out.push_back(v);
    }
    return out;
}

} // namespace

std::vector<BedGridSegment> bed_grid_lines(double x_min, double x_max, double y_min, double y_max,
                                           double spacing_mm, bool circular) {
    std::vector<BedGridSegment> lines;
    if (!(x_max > x_min) || !(y_max > y_min) || !(spacing_mm > 0.0)) {
        return lines;
    }

    if (!circular) {
        for (double x : interior_multiples(x_min, x_max, spacing_mm)) {
            lines.push_back({x, y_min, x, y_max});
        }
        for (double y : interior_multiples(y_min, y_max, spacing_mm)) {
            lines.push_back({x_min, y, x_max, y});
        }
        return lines;
    }

    const auto [cx, cy, r] = inscribed_round_bed(x_min, x_max, y_min, y_max);
    for (double x : interior_multiples(cx - r, cx + r, spacing_mm)) {
        const double half = std::sqrt(r * r - (x - cx) * (x - cx));
        lines.push_back({x, cy - half, x, cy + half});
    }
    for (double y : interior_multiples(cy - r, cy + r, spacing_mm)) {
        const double half = std::sqrt(r * r - (y - cy) * (y - cy));
        lines.push_back({cx - half, y, cx + half, y});
    }
    return lines;
}

std::vector<BedGridSegment> bed_outline_segments(double x_min, double x_max, double y_min,
                                                 double y_max, bool circular) {
    std::vector<BedGridSegment> out;
    if (!(x_max > x_min) || !(y_max > y_min)) {
        return out;
    }
    if (!circular) {
        out.push_back({x_min, y_min, x_max, y_min});
        out.push_back({x_max, y_min, x_max, y_max});
        out.push_back({x_max, y_max, x_min, y_max});
        out.push_back({x_min, y_max, x_min, y_min});
        return out;
    }
    constexpr double PI = 3.14159265358979323846;
    const auto [cx, cy, r] = inscribed_round_bed(x_min, x_max, y_min, y_max);
    for (int i = 0; i < kRoundBedSegments; i++) {
        const double a1 = 2.0 * PI * i / kRoundBedSegments;
        const double a2 = 2.0 * PI * (i + 1) / kRoundBedSegments;
        out.push_back({cx + r * std::cos(a1), cy + r * std::sin(a1), cx + r * std::cos(a2),
                       cy + r * std::sin(a2)});
    }
    return out;
}

RoundBed inscribed_round_bed(double x_min, double x_max, double y_min, double y_max) {
    return {(x_min + x_max) / 2.0, (y_min + y_max) / 2.0,
            std::min(x_max - x_min, y_max - y_min) / 2.0};
}

std::vector<BedPoint> clip_to_round_bed(const std::vector<BedPoint>& polygon, const RoundBed& bed) {
    constexpr double PI = 3.14159265358979323846;
    // Inside the inscribed polygon's apothem, nothing can be cut.
    const double apothem = bed.r * std::cos(PI / kRoundBedSegments);
    const bool all_inside = std::all_of(polygon.begin(), polygon.end(), [&](const BedPoint& p) {
        return std::hypot(p.x - bed.cx, p.y - bed.cy) <= apothem;
    });
    if (all_inside) {
        return polygon;
    }

    // Sutherland-Hodgman against each edge of the circle's polygon.
    std::vector<BedPoint> out = polygon;
    for (int i = 0; i < kRoundBedSegments && !out.empty(); i++) {
        const double a = 2.0 * PI * (i + 0.5) / kRoundBedSegments;
        const double nx = std::cos(a);
        const double ny = std::sin(a);
        // Signed distance inside the half-plane through this edge.
        auto inside = [&](const BedPoint& p) {
            return apothem - ((p.x - bed.cx) * nx + (p.y - bed.cy) * ny);
        };
        std::vector<BedPoint> in = std::move(out);
        out.clear();
        for (size_t j = 0; j < in.size(); j++) {
            const BedPoint& cur = in[j];
            const BedPoint& next = in[(j + 1) % in.size()];
            const double dc = inside(cur);
            const double dn = inside(next);
            if (dc >= 0.0) {
                out.push_back(cur);
            }
            if ((dc >= 0.0) != (dn >= 0.0)) {
                const double t = dc / (dc - dn);
                out.push_back({cur.x + t * (next.x - cur.x), cur.y + t * (next.y - cur.y)});
            }
        }
    }
    if (out.size() < 3) {
        out.clear();
    }
    return out;
}

std::optional<std::pair<double, double>> segment_in_round_bed(BedPoint a, BedPoint b,
                                                              const RoundBed& bed) {
    // |a + t(b-a) - c|^2 = r^2, solved for t.
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double fx = a.x - bed.cx;
    const double fy = a.y - bed.cy;
    const double qa = dx * dx + dy * dy;
    const double qb = 2.0 * (fx * dx + fy * dy);
    const double qc = fx * fx + fy * fy - bed.r * bed.r;
    if (qa <= 0.0) {
        return qc <= 0.0 ? std::optional<std::pair<double, double>>({0.0, 1.0}) : std::nullopt;
    }
    const double disc = qb * qb - 4.0 * qa * qc;
    if (disc <= 0.0) {
        return std::nullopt;
    }
    const double root = std::sqrt(disc);
    const double t0 = std::max(0.0, (-qb - root) / (2.0 * qa));
    const double t1 = std::min(1.0, (-qb + root) / (2.0 * qa));
    if (t0 >= t1) {
        return std::nullopt;
    }
    return std::make_pair(t0, t1);
}

} // namespace helix
