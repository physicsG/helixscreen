// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gcode_parser.h"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <limits>
#include <vector>

/**
 * @file gcode_crease_shading.h
 * @brief Crease darkening for the 2D preview, from neighbouring layers' walls.
 *
 * A wall segment's surface normal is estimated from where the outer walls of
 * the layers directly below and above it sit. Walking down to the wall below
 * and up to the wall above traces the wall's vertical profile; on a vertical
 * or evenly sloped wall those two steps point in opposite directions, and at a
 * crease (the edge of an overhang ledge, the rim where a wall meets a top
 * surface, a step inward) they bend. The bend angle is how sharply the normal
 * breaks, and that is what darkens the segment.
 *
 * Only outer walls take part, as both queries and neighbours: they are what the
 * preview shows of a model's surface, and an infill or top-skin segment has no
 * wall continuing it, so asking it for a bend would darken every top surface.
 */
namespace helix::gcode::crease {

/// Walls further than this from a segment do not continue it: the profile
/// treats the neighbour as absent, which reads as a right-angle edge. Past
/// about 5 layer heights the computed bend is within a few degrees of that
/// anyway, so a wider search would only cost lookups.
constexpr float kSearchRadiusMm = 1.0f;

/// Wall segments closer than this to the last lookup along the same wall reuse
/// its result. A crease changes along a wall no faster than the wall's shape
/// does, and slicers cut curves into segments a fraction of this long, so one
/// lookup per millimetre bounds the cost by wall length instead of segment
/// count.
constexpr float kSampleSpacingMm = 1.0f;
/// ...or closer than this many screen pixels, whichever is longer: a crease
/// that changes within a few pixels is invisible, and a whole model fitted to
/// a small canvas has walls far longer than its pixels.
constexpr float kSampleSpacingPx = 4.0f;

/// A wall segment shorter than this on screen, where a fresh lookup is due,
/// does not get one: it carries on the previous result when it continues the
/// same wall, and takes no crease when it starts a new one. Its pixels are too
/// few to show a change, and a model zoomed out far enough to hit this is
/// mostly such segments, so the lookups stay proportional to what the canvas
/// can show rather than to the file.
constexpr int kMinSamplePx = 3;

/// Lookup spacing along a wall at a given zoom (pixels per mm).
float sample_spacing_mm(float px_per_mm);

/// Horizontal steps shallower than this slope (about 22° off vertical), or
/// shorter than kJitterMm, count as no step at all. Slicer jitter and chord
/// error on a curved wall wobble by that much, and a wobble on each side of a
/// segment would otherwise add up to a bend past kBendStartRad.
constexpr float kJitterSlope = 0.4f;
constexpr float kJitterMm = 0.04f;

/// Steps shorter than this, `dz` below or above, snap to zero.
float snap_distance_mm(float dz);

/// Bends up to this angle leave the segment at full brightness.
constexpr float kBendStartRad = 0.52f; // 30°
/// Bends at or past this angle get the full darkening.
constexpr float kBendFullRad = 1.5708f; // 90°
/// Brightness multiplier at a full crease.
constexpr float kCreaseFloor = 0.55f;

/// A layer further than this from the one being shaded, in Z, is not its
/// neighbour. Sequential printing puts a later object's first layer straight
/// after an earlier object's top layer in file order, many millimetres away;
/// reading that as "the layer below" would make every first layer a ledge.
constexpr float kMaxNeighbourDzMm = 2.0f;

/// How many layers each side are searched for a continuing wall. Objects
/// sliced at different layer heights interleave in file order (0.2mm and
/// 0.3mm objects give layers at 0.2, 0.3, 0.4, 0.6, ...), so the next layer
/// down may hold only another object's walls; the one past it finds this
/// object again.
constexpr int kNeighbourDepth = 2;

/// Is a layer `dz` away (positive in the direction being searched) close
/// enough to count as a neighbour?
inline bool plausible_neighbour_dz(float dz) {
    return dz > 0.001f && dz <= kMaxNeighbourDzMm;
}

/// Does this segment take part in crease shading, as a query or a neighbour?
inline bool is_crease_wall(const ToolpathSegment& seg) {
    return seg.is_extrusion && (seg.feature_type == FeatureType::OuterWall ||
                                seg.feature_type == FeatureType::OverhangWall);
}

/// What the profile found on one side of a segment.
struct Neighbour {
    enum class Kind : uint8_t {
        NoLayer, ///< No plausible layer there (below the first layer, or too far in Z)
        Missing, ///< A layer, but no wall within kSearchRadiusMm
        Found,   ///< A wall; `offset` points from the segment to it
        Unknown, ///< A layer that could not be read: no evidence either way
    };
    Kind kind = Kind::NoLayer;
    glm::vec2 offset{0.0f, 0.0f}; ///< Horizontal step to the nearest wall (mm)
    float dz = 0.0f;              ///< Vertical distance to that layer (mm, positive)
};

/**
 * @brief Bend of a wall's vertical profile at one segment, in radians.
 *
 * 0 for a straight profile (vertical, or sloped at a constant angle), pi/2 for
 * a right-angle edge. A neighbour missing on ONE side is a right-angle edge
 * when the other side continues straight: the wall stops (a rim) or juts out
 * (an overhang ledge) by more than the search radius. Missing on BOTH sides is
 * a terrace step on a shallow slope, each layer stepping further than the
 * radius in the same direction; that is a straight surface, and darkening it
 * would stripe every gentle roof. Between the two, the found side's step eases
 * the bend down to zero as it nears the radius, so a slope stepping right
 * around the radius does not flicker between the two answers. No layer below
 * is the bed, which is not a crease, and an unreadable layer on either side
 * is no crease rather than a guess.
 */
float profile_bend(const Neighbour& below, const Neighbour& above);

/// Brightness multiplier for a bend: 1 up to kBendStartRad, easing down to
/// kCreaseFloor at kBendFullRad and beyond.
float crease_brightness(float bend_rad);

/**
 * @brief Spatial index of one layer's crease walls, for nearest-point lookups.
 *
 * Walls become pieces no longer than the search radius, bucketed by each
 * piece's midpoint in a fixed-size hashed grid. A long segment is split; runs
 * of short segments along one wall are merged into chords up to that length,
 * so a high-poly or fuzzy-skin wall costs no more than a smooth one: under two
 * pieces per radius of wall length, plus one per separate wall run. Memory is
 * bounded by wall length, never by segment count or bed size. Rebuilt in
 * place per layer; the vectors keep their capacity.
 */
class WallGrid {
  public:
    /// Index every crease wall in `segments`.
    void build(const std::vector<ToolpathSegment>& segments);
    /// Empty the index, keeping its buffers for the next build.
    void clear();
    /// Empty the index and give its buffers back.
    void release();

    bool empty() const {
        return pieces_.empty();
    }
    size_t piece_count() const {
        return pieces_.size();
    }

    /// Nearest point on any indexed wall to `p` within kSearchRadiusMm.
    /// Returns false when there is none.
    ///
    /// `hint`, when given, carries the piece the previous lookup landed on.
    /// Pieces are stored in wall order, so a query further along the same
    /// wall checks that piece and its neighbours first, and stops there
    /// when one is within `snap`: any nearer answer would snap to the same
    /// zero offset in find_neighbour().
    bool nearest(glm::vec2 p, glm::vec2& out, uint32_t* hint = nullptr, float snap = 0.0f) const;

    /// Bytes held by the grid's buffers (capacity, not size).
    size_t memory_bytes() const;

  private:
    static constexpr uint32_t kBuckets = 2048; // power of two
    static uint32_t bucket_of(int cx, int cy);

    std::vector<glm::vec4> pieces_;      // x0, y0, x1, y1
    std::vector<uint32_t> bucket_start_; // kBuckets + 1 offsets into items_
    std::vector<uint32_t> items_;        // piece indices, grouped by bucket
};

/**
 * @brief Spaces crease lookups along a wall (see sample_spacing_mm()).
 *
 * Feed it the layer's crease walls in drawing order. It tracks how far the
 * walls have run since the last lookup, gaps between segments included, so
 * segments skipped for drawing in between still count toward the spacing.
 */
class SampleSpacer {
  public:
    explicit SampleSpacer(float spacing_mm = kSampleSpacingMm) : spacing_(spacing_mm) {}

    /// True when `seg` needs its own lookup; false to reuse the last one.
    bool needs_sample(const ToolpathSegment& seg);
    void reset() {
        remaining_ = 0.0f;
        has_last_ = false;
        continued_ = false;
    }
    /// Whether the segment last passed in started where the one before ended.
    bool continued() const {
        return continued_;
    }

  private:
    float spacing_;
    bool has_last_ = false;
    bool continued_ = false;
    glm::vec2 last_end_{0.0f, 0.0f};
    float remaining_ = 0.0f;
};

/**
 * @brief The layers searched on one side of a layer, nearest first.
 *
 * Holds up to kNeighbourDepth grids that passed plausible_neighbour_dz(), with
 * each one's distance in Z and its own lookup hint.
 */
struct NeighbourSide {
    std::array<const WallGrid*, kNeighbourDepth> grids{};
    std::array<float, kNeighbourDepth> dz{};
    std::array<uint32_t, kNeighbourDepth> hints = make_hints();
    int count = 0;
    /// A layer on this side could not be read, so the side proves nothing.
    bool unreadable = false;

    void add(const WallGrid* grid, float layer_dz) {
        if (count < kNeighbourDepth) {
            grids[count] = grid;
            dz[count] = layer_dz;
            ++count;
        }
    }

    /// The nearest layer with a wall near `p`: Found from the first layer
    /// that has one, Missing when none does, NoLayer when no layer was
    /// plausible, Unknown when one was unreadable.
    Neighbour lookup(glm::vec2 p);

  private:
    static std::array<uint32_t, kNeighbourDepth> make_hints() {
        std::array<uint32_t, kNeighbourDepth> h{};
        h.fill(std::numeric_limits<uint32_t>::max());
        return h;
    }
};

/// Look up a point against one neighbouring layer's grid, `dz` away in Z.
/// A null grid means there is no layer there. `hint` is passed through to
/// WallGrid::nearest(); keep one per grid.
Neighbour find_neighbour(const WallGrid* grid, glm::vec2 p, float dz, uint32_t* hint = nullptr);

} // namespace helix::gcode::crease
