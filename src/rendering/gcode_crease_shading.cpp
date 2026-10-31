// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gcode_crease_shading.h"

#include "gcode_projection.h"

#include <algorithm>
#include <cmath>

namespace helix::gcode::crease {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kRightAngle = kPi * 0.5f;

/// Grid cells are one search radius wide, and walls are split into pieces no
/// longer than that, so every point of a piece is within half a cell of the
/// midpoint it is bucketed by.
constexpr float kCell = kSearchRadiusMm;
constexpr float kInvCell = 1.0f / kCell;

/// Pieces either side of the hint a lookup checks before searching the grid.
/// Lookups are about kSampleSpacingMm apart and pieces at most a cell long,
/// so the next answer along a wall is usually within a couple of pieces.
constexpr uint32_t kHintReach = 3;

int cell_of(float v) {
    return static_cast<int>(std::floor(v * kInvCell));
}

/// Visit the pieces build() indexes, in wall order: long segments split into
/// pieces of at most a cell, consecutive short segments of one wall merged into
/// chords of at most a cell of path length. The chord is within half a cell of
/// its midpoint, which is what the bucketing relies on.
template <typename Emit>
void for_each_piece(const std::vector<ToolpathSegment>& segments, Emit&& emit) {
    bool open = false;
    glm::vec2 run_start(0.0f), run_end(0.0f);
    float run_len = 0.0f;
    auto flush = [&]() {
        if (open) {
            emit(run_start, run_end);
            open = false;
        }
    };
    for (const auto& seg : segments) {
        if (!is_crease_wall(seg)) {
            flush();
            continue;
        }
        const glm::vec2 a(seg.start.x, seg.start.y);
        const glm::vec2 b(seg.end.x, seg.end.y);
        const float len = glm::length(b - a);
        if (len > kCell) {
            flush();
            const int pieces = static_cast<int>(std::ceil(len * kInvCell));
            const glm::vec2 step = (b - a) / static_cast<float>(pieces);
            glm::vec2 p0 = a;
            for (int i = 0; i < pieces; ++i) {
                const glm::vec2 p1 = a + step * static_cast<float>(i + 1);
                emit(p0, p1);
                p0 = p1;
            }
            continue;
        }
        if (open && a == run_end && run_len + len <= kCell) {
            run_end = b;
            run_len += len;
            continue;
        }
        flush();
        run_start = a;
        run_end = b;
        run_len = len;
        open = true;
    }
    flush();
}

} // namespace

float profile_bend(const Neighbour& below, const Neighbour& above) {
    if (below.kind == Neighbour::Kind::NoLayer || below.kind == Neighbour::Kind::Unknown ||
        above.kind == Neighbour::Kind::Unknown) {
        return 0.0f;
    }
    const bool below_found = below.kind == Neighbour::Kind::Found;
    const bool above_found = above.kind == Neighbour::Kind::Found;
    if (!below_found && !above_found) {
        return 0.0f;
    }
    if (!below_found || !above_found) {
        // The found side's own step says how far from a terrace this is: a
        // vertical wall meeting nothing is a right angle, one already
        // stepping nearly a radius per layer is a slope whose next step
        // happened to land just past the search.
        const Neighbour& found = below_found ? below : above;
        const float step = std::min(glm::length(found.offset) / kSearchRadiusMm, 1.0f);
        return kRightAngle * (1.0f - step);
    }

    // Profile tangents from this segment down to the wall below and up to the
    // wall above. A straight profile makes them antiparallel (angle pi).
    const glm::vec3 down(below.offset, -below.dz);
    const glm::vec3 up(above.offset, above.dz);
    const float len = glm::length(down) * glm::length(up);
    if (len <= 0.0f) {
        return 0.0f;
    }
    const float cos_angle = std::clamp(glm::dot(down, up) / len, -1.0f, 1.0f);
    return kPi - std::acos(cos_angle);
}

float crease_brightness(float bend_rad) {
    const float t =
        std::clamp((bend_rad - kBendStartRad) / (kBendFullRad - kBendStartRad), 0.0f, 1.0f);
    const float eased = t * t * (3.0f - 2.0f * t);
    return 1.0f - eased * (1.0f - kCreaseFloor);
}

uint32_t WallGrid::bucket_of(int cx, int cy) {
    const uint32_t h =
        (static_cast<uint32_t>(cx) * 73856093u) ^ (static_cast<uint32_t>(cy) * 19349663u);
    return h & (kBuckets - 1);
}

void WallGrid::clear() {
    pieces_.clear();
    items_.clear();
    bucket_start_.clear();
}

void WallGrid::release() {
    clear();
    pieces_.shrink_to_fit();
    items_.shrink_to_fit();
    bucket_start_.shrink_to_fit();
}

void WallGrid::build(const std::vector<ToolpathSegment>& segments) {
    clear();

    // Sized up front: growing by doubling would hold up to twice the pieces
    // the largest layer needs for the rest of the build.
    size_t total = 0;
    for_each_piece(segments, [&](glm::vec2, glm::vec2) { ++total; });
    pieces_.resize(total);
    size_t next = 0;
    for_each_piece(segments, [&](glm::vec2 p0, glm::vec2 p1) {
        pieces_[next++] = glm::vec4(p0.x, p0.y, p1.x, p1.y);
    });
    if (pieces_.empty()) {
        return;
    }

    // Counting sort of pieces into buckets by midpoint cell. Counts become
    // bucket ends after the prefix sum, and filling backwards from each end
    // leaves every entry at its bucket's start.
    bucket_start_.assign(kBuckets + 1, 0);
    auto piece_bucket = [this](size_t i) {
        const glm::vec4& pc = pieces_[i];
        return bucket_of(cell_of((pc.x + pc.z) * 0.5f), cell_of((pc.y + pc.w) * 0.5f));
    };
    for (size_t i = 0; i < pieces_.size(); ++i) {
        ++bucket_start_[piece_bucket(i)];
    }
    for (uint32_t b = 1; b < kBuckets; ++b) {
        bucket_start_[b] += bucket_start_[b - 1];
    }
    bucket_start_[kBuckets] = static_cast<uint32_t>(pieces_.size());
    items_.resize(pieces_.size());
    for (size_t i = pieces_.size(); i-- > 0;) {
        items_[--bucket_start_[piece_bucket(i)]] = static_cast<uint32_t>(i);
    }
}

bool WallGrid::nearest(glm::vec2 p, glm::vec2& out, uint32_t* hint, float snap) const {
    if (pieces_.empty()) {
        return false;
    }
    float best = kSearchRadiusMm * kSearchRadiusMm;
    uint32_t best_piece = 0;
    bool found = false;
    auto consider = [&](uint32_t piece) {
        const glm::vec4& pc = pieces_[piece];
        const glm::vec2 c = closest_point_on_segment(p, {pc.x, pc.y}, {pc.z, pc.w});
        const glm::vec2 d = c - p;
        const float dist2 = glm::dot(d, d);
        if (dist2 <= best) {
            best = dist2;
            best_piece = piece;
            out = c;
            found = true;
        }
    };
    auto scan = [&](int cx, int cy) {
        const uint32_t bucket = bucket_of(cx, cy);
        for (uint32_t k = bucket_start_[bucket]; k < bucket_start_[bucket + 1]; ++k) {
            consider(items_[k]);
        }
    };

    if (hint && *hint < pieces_.size()) {
        const uint32_t h = *hint;
        const uint32_t lo = h > kHintReach ? h - kHintReach : 0;
        const uint32_t hi = std::min<uint32_t>(h + kHintReach, pieces_.size() - 1);
        for (uint32_t i = lo; i <= hi; ++i) {
            consider(i);
        }
        if (found && best < snap * snap) {
            *hint = best_piece;
            return true;
        }
    }

    // The query's own cell first: on a wall that continues straight up, the
    // answer is usually there and close, which shrinks the ring to check.
    const int pcx = cell_of(p.x), pcy = cell_of(p.y);
    scan(pcx, pcy);

    // A piece nearer than the best so far has its midpoint within that
    // distance plus half a cell, so only those cells can improve on it.
    const float reach = std::sqrt(best) + kCell * 0.5f;
    const int x0 = cell_of(p.x - reach), x1 = cell_of(p.x + reach);
    const int y0 = cell_of(p.y - reach), y1 = cell_of(p.y + reach);
    for (int cy = y0; cy <= y1; ++cy) {
        for (int cx = x0; cx <= x1; ++cx) {
            if (cx != pcx || cy != pcy) {
                scan(cx, cy);
            }
        }
    }
    if (found && hint) {
        *hint = best_piece;
    }
    return found;
}

size_t WallGrid::memory_bytes() const {
    return pieces_.capacity() * sizeof(glm::vec4) +
           (bucket_start_.capacity() + items_.capacity()) * sizeof(uint32_t);
}

float snap_distance_mm(float dz) {
    return std::max(kJitterMm, kJitterSlope * dz);
}

float sample_spacing_mm(float px_per_mm) {
    if (px_per_mm <= 0.0f) {
        return kSampleSpacingMm;
    }
    return std::max(kSampleSpacingMm, kSampleSpacingPx / px_per_mm);
}

bool SampleSpacer::needs_sample(const ToolpathSegment& seg) {
    const glm::vec2 a(seg.start.x, seg.start.y);
    const glm::vec2 b(seg.end.x, seg.end.y);
    const float len = glm::length(b - a);
    const float gap = glm::length(a - last_end_);
    const float travelled = gap + len;
    continued_ = has_last_ && gap < kJitterMm;
    has_last_ = true;
    last_end_ = b;
    if (travelled <= remaining_) {
        remaining_ -= travelled;
        return false;
    }
    // The lookup samples this segment's midpoint.
    remaining_ = spacing_ - len * 0.5f;
    return true;
}

Neighbour find_neighbour(const WallGrid* grid, glm::vec2 p, float dz, uint32_t* hint) {
    Neighbour n;
    if (!grid) {
        return n;
    }
    n.dz = dz;
    glm::vec2 hit;
    const float snap = snap_distance_mm(n.dz);
    if (!grid->nearest(p, hit, hint, snap)) {
        n.kind = Neighbour::Kind::Missing;
        return n;
    }
    n.kind = Neighbour::Kind::Found;
    n.offset = hit - p;
    if (glm::dot(n.offset, n.offset) < snap * snap) {
        n.offset = glm::vec2(0.0f);
    }
    return n;
}

Neighbour NeighbourSide::lookup(glm::vec2 p) {
    if (unreadable) {
        Neighbour n;
        n.kind = Neighbour::Kind::Unknown;
        return n;
    }
    Neighbour result; // NoLayer when nothing was plausible
    for (int k = 0; k < count; ++k) {
        const Neighbour n = find_neighbour(grids[k], p, dz[k], &hints[k]);
        if (n.kind == Neighbour::Kind::Found) {
            return n;
        }
        result = n;
    }
    return result;
}

} // namespace helix::gcode::crease
