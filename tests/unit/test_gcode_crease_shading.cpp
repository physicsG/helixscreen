// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "../test_helpers/gcode_layer_renderer_test_access.h"
#include "../test_helpers/streaming_test_sources.h"
#include "gcode_crease_shading.h"
#include "gcode_data_source.h"
#include "gcode_layer_renderer.h"
#include "gcode_streaming_controller.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::gcode;
using namespace helix::gcode::crease;
using namespace helix::gcode::test;
using Catch::Approx;

namespace {

constexpr float kDz = 0.2f;
constexpr float kDeg = 3.14159265f / 180.0f;

Neighbour found(glm::vec2 offset, float dz = kDz) {
    Neighbour n;
    n.kind = Neighbour::Kind::Found;
    n.offset = offset;
    n.dz = dz;
    return n;
}

Neighbour missing() {
    Neighbour n;
    n.kind = Neighbour::Kind::Missing;
    n.dz = kDz;
    return n;
}

ToolpathSegment wall(glm::vec2 a, glm::vec2 b, float z, FeatureType type = FeatureType::OuterWall) {
    ToolpathSegment s;
    s.start = {a.x, a.y, z};
    s.end = {b.x, b.y, z};
    s.is_extrusion = true;
    s.feature_type = type;
    return s;
}

/// Outer wall of an axis-aligned square, x/y in [lo, hi].
std::vector<ToolpathSegment> square(float lo, float hi, float z,
                                    FeatureType type = FeatureType::OuterWall) {
    return {wall({lo, lo}, {hi, lo}, z, type), wall({hi, lo}, {hi, hi}, z, type),
            wall({hi, hi}, {lo, hi}, z, type), wall({lo, hi}, {lo, lo}, z, type)};
}

} // namespace

TEST_CASE("crease: a flat vertical wall bends nowhere and stays at full brightness",
          "[gcode][crease]") {
    const float bend = profile_bend(found({0.0f, 0.0f}), found({0.0f, 0.0f}));
    REQUIRE(bend == Approx(0.0f).margin(1e-4));
    REQUIRE(crease_brightness(bend) == Approx(1.0f));
}

TEST_CASE("crease: an evenly sloped wall is straight, not a crease", "[gcode][crease]") {
    // Flaring outward 0.15mm per layer: the wall below sits inward, the wall
    // above sits outward, by the same step.
    const float bend = profile_bend(found({-0.15f, 0.0f}), found({0.15f, 0.0f}));
    REQUIRE(bend == Approx(0.0f).margin(1e-3));
    REQUIRE(crease_brightness(bend) == Approx(1.0f));
}

TEST_CASE("crease: a right-angle edge gets the full darkening", "[gcode][crease]") {
    SECTION("the wall stops: nothing above, as at a top surface's rim") {
        const float bend = profile_bend(found({0.0f, 0.0f}), missing());
        REQUIRE(bend == Approx(90.0f * kDeg).margin(1e-3));
        REQUIRE(crease_brightness(bend) == Approx(kCreaseFloor));
    }
    SECTION("the top layer of the file: no layer above at all") {
        const float bend = profile_bend(found({0.0f, 0.0f}), Neighbour{});
        REQUIRE(bend == Approx(90.0f * kDeg).margin(1e-3));
    }
    SECTION("a step inward just inside the search radius is nearly as sharp") {
        const float bend = profile_bend(found({0.0f, 0.0f}), found({-0.9f, 0.0f}));
        REQUIRE(bend > 75.0f * kDeg);
        REQUIRE(crease_brightness(bend) < 0.62f);
    }
}

TEST_CASE("crease: an overhang ledge darkens", "[gcode][crease]") {
    SECTION("juts out past the search radius: no wall below") {
        const float bend = profile_bend(missing(), found({0.0f, 0.0f}));
        REQUIRE(bend == Approx(90.0f * kDeg).margin(1e-3));
        REQUIRE(crease_brightness(bend) == Approx(kCreaseFloor));
    }
    SECTION("juts out 0.6mm: the wall below is inward, the wall above is vertical") {
        const float bend = profile_bend(found({-0.6f, 0.0f}), found({0.0f, 0.0f}));
        REQUIRE(bend > 60.0f * kDeg);
        REQUIRE(crease_brightness(bend) < 0.8f);
    }
}

TEST_CASE("crease: a single-layer fin is sharper than a right angle", "[gcode][crease]") {
    // Both neighbours sit inward: the profile folds back on itself.
    const float bend = profile_bend(found({-0.8f, 0.0f}), found({-0.8f, 0.0f}));
    REQUIRE(bend > 140.0f * kDeg);
    REQUIRE(crease_brightness(bend) == Approx(kCreaseFloor));
}

TEST_CASE("crease: a terrace step on a shallow slope is not a crease", "[gcode][crease]") {
    // Each layer steps further than the search radius, in the same direction
    // on both sides: a gentle roof, which must not come out striped.
    REQUIRE(profile_bend(missing(), missing()) == 0.0f);
    REQUIRE(profile_bend(missing(), Neighbour{}) == 0.0f);
}

TEST_CASE("crease: a slope stepping about a radius per layer does not flicker", "[gcode][crease]") {
    // One side lands just inside the search, the other just past it: still a
    // terrace, and nowhere near the right angle a vertical wall would give.
    const float near_radius = profile_bend(found({0.95f, 0.0f}), missing());
    REQUIRE(near_radius < kBendStartRad);
    REQUIRE(crease_brightness(near_radius) == Approx(1.0f));

    // The ease is monotonic: the steeper the found side, the sharper the edge.
    REQUIRE(profile_bend(found({0.3f, 0.0f}), missing()) >
            profile_bend(found({0.6f, 0.0f}), missing()));
    REQUIRE(profile_bend(missing(), found({0.0f, 0.0f})) >
            profile_bend(missing(), found({0.3f, 0.0f})));
}

TEST_CASE("crease: the first layer sits on the bed, which is not a crease", "[gcode][crease]") {
    REQUIRE(profile_bend(Neighbour{}, found({0.0f, 0.0f})) == 0.0f);
    REQUIRE(profile_bend(Neighbour{}, missing()) == 0.0f);
}

TEST_CASE("crease: brightness eases from 1 to the floor between the two thresholds",
          "[gcode][crease]") {
    REQUIRE(crease_brightness(0.0f) == Approx(1.0f));
    REQUIRE(crease_brightness(kBendStartRad) == Approx(1.0f));
    REQUIRE(crease_brightness(kBendFullRad) == Approx(kCreaseFloor));
    REQUIRE(crease_brightness(3.0f) == Approx(kCreaseFloor));

    const float mid = crease_brightness((kBendStartRad + kBendFullRad) * 0.5f);
    REQUIRE(mid == Approx((1.0f + kCreaseFloor) * 0.5f).margin(1e-3));

    float prev = 1.0f;
    for (float b = 0.0f; b <= 1.8f; b += 0.05f) {
        const float v = crease_brightness(b);
        REQUIRE(v <= prev + 1e-6f);
        prev = v;
    }
}

TEST_CASE("crease: walls only take part", "[gcode][crease]") {
    ToolpathSegment s = wall({0, 0}, {1, 0}, 0.2f);
    REQUIRE(is_crease_wall(s));
    s.feature_type = FeatureType::OverhangWall;
    REQUIRE(is_crease_wall(s));
    for (FeatureType t : {FeatureType::InnerWall, FeatureType::TopSurface, FeatureType::SolidInfill,
                          FeatureType::SparseInfill, FeatureType::Unknown, FeatureType::Bridge}) {
        s.feature_type = t;
        REQUIRE_FALSE(is_crease_wall(s));
    }
    s.feature_type = FeatureType::OuterWall;
    s.is_extrusion = false;
    REQUIRE_FALSE(is_crease_wall(s));
}

TEST_CASE("crease: lookups are spaced along a wall, not per segment", "[gcode][crease]") {
    SampleSpacer spacer;

    SECTION("a curve cut into 0.1mm segments samples about once per millimetre") {
        int samples = 0;
        for (int i = 0; i < 100; ++i) {
            const float x = 0.1f * static_cast<float>(i);
            samples += spacer.needs_sample(wall({x, 0.0f}, {x + 0.1f, 0.0f}, 0.2f)) ? 1 : 0;
        }
        REQUIRE(samples >= 9);
        REQUIRE(samples <= 11);
    }
    SECTION("segments longer than the spacing each sample") {
        int samples = 0;
        for (int i = 0; i < 5; ++i) {
            const float x = 3.0f * static_cast<float>(i);
            samples += spacer.needs_sample(wall({x, 0.0f}, {x + 3.0f, 0.0f}, 0.2f)) ? 1 : 0;
        }
        REQUIRE(samples == 5);
    }
    SECTION("a jump to another wall samples again") {
        REQUIRE(spacer.needs_sample(wall({0.0f, 0.0f}, {0.1f, 0.0f}, 0.2f)));
        REQUIRE_FALSE(spacer.needs_sample(wall({0.1f, 0.0f}, {0.2f, 0.0f}, 0.2f)));
        REQUIRE(spacer.needs_sample(wall({20.0f, 0.0f}, {20.1f, 0.0f}, 0.2f)));
    }
    SECTION("it knows whether a segment continues the last one") {
        spacer.needs_sample(wall({0.0f, 0.0f}, {0.1f, 0.0f}, 0.2f));
        REQUIRE_FALSE(spacer.continued());
        spacer.needs_sample(wall({0.1f, 0.0f}, {0.2f, 0.0f}, 0.2f));
        REQUIRE(spacer.continued());
        spacer.needs_sample(wall({5.0f, 0.0f}, {5.1f, 0.0f}, 0.2f));
        REQUIRE_FALSE(spacer.continued());
        spacer.reset();
        spacer.needs_sample(wall({5.1f, 0.0f}, {5.2f, 0.0f}, 0.2f));
        REQUIRE_FALSE(spacer.continued());
    }
    SECTION("reset forgets the last lookup") {
        REQUIRE(spacer.needs_sample(wall({0.0f, 0.0f}, {0.1f, 0.0f}, 0.2f)));
        spacer.reset();
        REQUIRE(spacer.needs_sample(wall({0.1f, 0.0f}, {0.2f, 0.0f}, 0.2f)));
    }
}

TEST_CASE("crease: lookup spacing widens when the model is zoomed out", "[gcode][crease]") {
    // Zoomed in, the millimetre floor holds.
    REQUIRE(sample_spacing_mm(10.0f) == Approx(kSampleSpacingMm));
    // At one pixel per mm, kSampleSpacingPx pixels is the longer of the two.
    REQUIRE(sample_spacing_mm(1.0f) == Approx(kSampleSpacingPx));
    REQUIRE(sample_spacing_mm(0.0f) == Approx(kSampleSpacingMm));

    // A wider spacing samples a curve less often.
    SampleSpacer wide(sample_spacing_mm(1.0f));
    int samples = 0;
    for (int i = 0; i < 100; ++i) {
        const float x = 0.1f * static_cast<float>(i);
        samples += wide.needs_sample(wall({x, 0.0f}, {x + 0.1f, 0.0f}, 0.2f)) ? 1 : 0;
    }
    REQUIRE(samples >= 2);
    REQUIRE(samples <= 4);
}

TEST_CASE("crease: WallGrid finds the nearest point on a wall within the radius",
          "[gcode][crease]") {
    WallGrid grid;
    REQUIRE(grid.empty());
    glm::vec2 hit;
    REQUIRE_FALSE(grid.nearest({0, 0}, hit));

    grid.build(square(10.0f, 30.0f, 0.4f));
    REQUIRE_FALSE(grid.empty());
    REQUIRE(grid.piece_count() == 80); // four 20mm edges in 1mm pieces

    SECTION("the middle of a long edge is found: walls are indexed along their length") {
        REQUIRE(grid.nearest({20.0f, 10.3f}, hit));
        REQUIRE(hit.x == Approx(20.0f));
        REQUIRE(hit.y == Approx(10.0f));
    }
    SECTION("the closer of two walls wins near a corner") {
        REQUIRE(grid.nearest({29.6f, 29.9f}, hit));
        REQUIRE(hit.x == Approx(29.6f));
        REQUIRE(hit.y == Approx(30.0f));
    }
    SECTION("outside the radius there is nothing") {
        REQUIRE_FALSE(grid.nearest({20.0f, 20.0f}, hit));
        REQUIRE_FALSE(grid.nearest({20.0f, 8.8f}, hit));
    }
    SECTION("at negative coordinates too") {
        WallGrid neg;
        neg.build(square(-30.0f, -10.0f, 0.2f));
        REQUIRE(neg.nearest({-20.0f, -9.5f}, hit));
        REQUIRE(hit.y == Approx(-10.0f));
    }
}

TEST_CASE("crease: a hinted lookup gives the same answer as a full one", "[gcode][crease]") {
    WallGrid grid;
    grid.build(square(10.0f, 30.0f, 0.2f));

    uint32_t hint = UINT32_MAX;
    glm::vec2 hit, plain;
    // Walk along the bottom edge, then jump to the far one: the hint must
    // follow along the wall and not pin a stale piece after the jump.
    for (float x = 10.5f; x < 30.0f; x += 0.7f) {
        const glm::vec2 p(x, 10.02f);
        REQUIRE(grid.nearest(p, hit, &hint, snap_distance_mm(kDz)));
        REQUIRE(grid.nearest(p, plain));
        REQUIRE(glm::length(hit - p) < snap_distance_mm(kDz));
        REQUIRE(glm::length(plain - p) < snap_distance_mm(kDz));
    }
    const glm::vec2 far(20.0f, 29.5f);
    REQUIRE(grid.nearest(far, hit, &hint, snap_distance_mm(kDz)));
    REQUIRE(hit.y == Approx(30.0f));
    REQUIRE(hit.x == Approx(20.0f));

    // A hint pointing off the end is ignored, not read.
    hint = 1000000;
    REQUIRE(grid.nearest({20.0f, 10.3f}, hit, &hint));
    REQUIRE(hit.y == Approx(10.0f));
}

TEST_CASE("crease: WallGrid ignores everything but walls", "[gcode][crease]") {
    WallGrid grid;
    auto segs = square(10.0f, 30.0f, 0.2f, FeatureType::InnerWall);
    segs.push_back(wall({10, 20}, {30, 20}, 0.2f, FeatureType::TopSurface));
    grid.build(segs);
    REQUIRE(grid.empty());
    glm::vec2 hit;
    REQUIRE_FALSE(grid.nearest({20.0f, 10.0f}, hit));
}

TEST_CASE("crease: WallGrid rebuilds in place for the next layer", "[gcode][crease]") {
    WallGrid grid;
    grid.build(square(10.0f, 30.0f, 0.2f));
    grid.build(square(50.0f, 70.0f, 0.4f));
    glm::vec2 hit;
    REQUIRE_FALSE(grid.nearest({20.0f, 10.0f}, hit));
    REQUIRE(grid.nearest({60.0f, 50.2f}, hit));
    REQUIRE(grid.piece_count() == 80);
    REQUIRE(grid.memory_bytes() > 0);

    grid.release();
    REQUIRE(grid.empty());
    REQUIRE(grid.memory_bytes() == 0);
    REQUIRE_FALSE(grid.nearest({60.0f, 50.2f}, hit));
}

TEST_CASE("crease: steps under about 22 degrees off vertical snap to none", "[gcode][crease]") {
    REQUIRE(snap_distance_mm(0.2f) == Approx(0.08f));
    REQUIRE(snap_distance_mm(0.3f) == Approx(0.12f));
    // Thin layers keep a floor for slicer jitter.
    REQUIRE(snap_distance_mm(0.05f) == Approx(kJitterMm));
}

TEST_CASE("crease: find_neighbour classifies each side", "[gcode][crease]") {
    WallGrid grid;
    grid.build(square(10.0f, 30.0f, 0.4f));

    REQUIRE(find_neighbour(nullptr, {20, 10}, 0.2f).kind == Neighbour::Kind::NoLayer);

    const Neighbour far = find_neighbour(&grid, {20.0f, 5.0f}, 0.2f);
    REQUIRE(far.kind == Neighbour::Kind::Missing);

    const Neighbour near = find_neighbour(&grid, {20.0f, 9.5f}, 0.2f);
    REQUIRE(near.kind == Neighbour::Kind::Found);
    REQUIRE(near.offset.x == Approx(0.0f).margin(1e-5));
    REQUIRE(near.offset.y == Approx(0.5f));
    REQUIRE(near.dz == Approx(0.2f));

    // Slicer jitter on a vertical wall reads as no step at all.
    const Neighbour jitter = find_neighbour(&grid, {20.0f, 10.07f}, 0.2f);
    REQUIRE(jitter.kind == Neighbour::Kind::Found);
    REQUIRE(jitter.offset == glm::vec2(0.0f));
    REQUIRE(jitter.dz == Approx(0.2f));

    // An empty layer is a layer with no wall near.
    WallGrid empty;
    REQUIRE(find_neighbour(&empty, {20, 10}, 0.2f).kind == Neighbour::Kind::Missing);
}

TEST_CASE("crease: an unreadable neighbour is no evidence, not an edge", "[gcode][crease]") {
    Neighbour unknown;
    unknown.kind = Neighbour::Kind::Unknown;
    REQUIRE(profile_bend(found({0.0f, 0.0f}), unknown) == 0.0f);
    REQUIRE(profile_bend(unknown, found({0.0f, 0.0f})) == 0.0f);
    REQUIRE(profile_bend(unknown, missing()) == 0.0f);
}

TEST_CASE("crease: only layers just beyond in Z count as neighbours", "[gcode][crease]") {
    REQUIRE(plausible_neighbour_dz(0.2f));
    REQUIRE(plausible_neighbour_dz(kMaxNeighbourDzMm));
    // Wrong direction: a sequential print's next object starts back at the bed.
    REQUIRE_FALSE(plausible_neighbour_dz(-18.0f));
    REQUIRE_FALSE(plausible_neighbour_dz(0.0f));
    // Too far: the top of another object many millimetres away.
    REQUIRE_FALSE(plausible_neighbour_dz(kMaxNeighbourDzMm + 0.5f));
}

TEST_CASE("crease: a side searches past a layer holding only other walls", "[gcode][crease]") {
    WallGrid other, own;
    other.build(square(60.0f, 80.0f, 0.3f)); // another object's layer
    own.build(square(10.0f, 30.0f, 0.2f));
    const glm::vec2 p(20.0f, 10.0f);

    SECTION("the nearer layer misses, the next one finds this object") {
        NeighbourSide side;
        side.add(&other, 0.1f);
        side.add(&own, 0.2f);
        const Neighbour n = side.lookup(p);
        REQUIRE(n.kind == Neighbour::Kind::Found);
        REQUIRE(n.dz == Approx(0.2f));
    }
    SECTION("no layer has a wall near: Missing") {
        NeighbourSide side;
        side.add(&other, 0.1f);
        REQUIRE(side.lookup(p).kind == Neighbour::Kind::Missing);
    }
    SECTION("no plausible layer at all: NoLayer") {
        NeighbourSide side;
        REQUIRE(side.lookup(p).kind == Neighbour::Kind::NoLayer);
    }
    SECTION("an unreadable layer: Unknown, whatever else was found") {
        NeighbourSide side;
        side.add(&own, 0.2f);
        side.unreadable = true;
        REQUIRE(side.lookup(p).kind == Neighbour::Kind::Unknown);
    }
}

TEST_CASE("crease: a high-poly wall indexes by length, not by segment count", "[gcode][crease]") {
    constexpr float kR = 10.0f;
    constexpr float kTwoPi = 6.2831853f;
    // segments around the circle, and radial jitter (fuzzy-skin style)
    struct Case {
        int segs;
        float jitter;
    };
    for (const Case c : {Case{2000, 0.0f}, Case{160, 0.3f}}) {
        std::vector<ToolpathSegment> segs;
        float path = 0.0f;
        glm::vec2 prev(kR, 0.0f);
        for (int i = 1; i <= c.segs; ++i) {
            const float a = kTwoPi * static_cast<float>(i) / static_cast<float>(c.segs);
            const float wobble =
                i % c.segs == 0 ? 0.0f
                                : c.jitter * (static_cast<float>((i * 7919) % 7) / 3.0f - 1.0f);
            const glm::vec2 cur((kR + wobble) * std::cos(a), (kR + wobble) * std::sin(a));
            segs.push_back(wall(prev, cur, 0.2f));
            path += glm::length(cur - prev);
            prev = cur;
        }
        WallGrid grid;
        grid.build(segs);
        // Under two pieces per radius of path, plus one for the run.
        REQUIRE(grid.piece_count() > 0);
        REQUIRE(grid.piece_count() <= static_cast<size_t>(2.0f * path / kSearchRadiusMm) + 1);
        glm::vec2 hit;
        const glm::vec2 q(kR + 0.5f, 0.3f);
        REQUIRE(grid.nearest(q, hit));
        REQUIRE(glm::length(hit - q) < 0.5f + c.jitter + 0.05f);
    }

    // The smooth 2000-segment circle collapses to about its length in pieces.
    std::vector<ToolpathSegment> smooth;
    glm::vec2 prev(kR, 0.0f);
    for (int i = 1; i <= 2000; ++i) {
        const float a = kTwoPi * static_cast<float>(i) / 2000.0f;
        const glm::vec2 cur(kR * std::cos(a), kR * std::sin(a));
        smooth.push_back(wall(prev, cur, 0.2f));
        prev = cur;
    }
    WallGrid grid;
    grid.build(smooth);
    REQUIRE(grid.piece_count() < 2000 / 10);
}

TEST_CASE("crease: separate walls are not merged across a gap", "[gcode][crease]") {
    std::vector<ToolpathSegment> segs = {wall({0.0f, 0.0f}, {0.3f, 0.0f}, 0.2f),
                                         wall({5.0f, 0.0f}, {5.3f, 0.0f}, 0.2f)};
    WallGrid grid;
    grid.build(segs);
    REQUIRE(grid.piece_count() == 2);
    glm::vec2 hit;
    // A merged chord from 0 to 5.3 would put a wall under x=2.5.
    REQUIRE_FALSE(grid.nearest({2.5f, 0.0f}, hit));
}

// ---------------------------------------------------------------------------
// Wiring: the solid cache build applies the crease to wall colours.
// ---------------------------------------------------------------------------

namespace {

constexpr int kCanvasPx = 240;
constexpr float kLayerMm = 1.0f; // tall layers so each one owns a few pixel rows

/// Layers 0-9 a 20mm box, layers 10-19 a 30mm box around it: layer 10 is an
/// overhang ledge and layer 9 is the rim the wall steps out from.
ParsedGCodeFile make_stepped_tower(FeatureType type) {
    ParsedGCodeFile gcode;
    for (int i = 0; i < 20; ++i) {
        Layer layer;
        layer.z_height = kLayerMm * static_cast<float>(i + 1);
        layer.segments = i < 10 ? square(20.0f, 40.0f, layer.z_height, type)
                                : square(15.0f, 45.0f, layer.z_height, type);
        gcode.layers.push_back(std::move(layer));
    }
    return gcode;
}

uint32_t luma(uint32_t argb) {
    return ((argb >> 16) & 0xFF) + ((argb >> 8) & 0xFF) + (argb & 0xFF);
}

struct WallProbe {
    int visible = 0; ///< wall midpoints that landed on painted pixels
    int darker = 0;  ///< ...that the crease made darker
    int lighter = 0; ///< ...that the crease made lighter
};

/// Render layers 0..top with crease shading off and on, and compare the pixel
/// under each of `mids` (points on layer `top`'s walls, which nothing later
/// paints over). `attach` points a renderer at its data.
template <typename Attach>
WallProbe probe_walls(Attach&& attach, int top, const std::vector<glm::vec3>& mids) {
    std::vector<uint32_t> px[2];
    for (int on = 0; on < 2; ++on) {
        GCodeLayerRenderer r;
        attach(r);
        r.set_ghost_mode(false);
        r.set_crease_shading(on == 1);
        r.set_canvas_size(kCanvasPx, kCanvasPx);
        r.auto_fit();
        GCodeLayerRendererTestAccess::pin_layers_per_frame(r, 100);
        // A layer may wait one frame for a streamed neighbour; a few batches
        // always reach the top.
        int done = -1;
        for (int i = 0; i < 8 && done < top; ++i) {
            done = GCodeLayerRendererTestAccess::render_solid_batch(r, done + 1, top, kCanvasPx,
                                                                    kCanvasPx);
        }
        REQUIRE(done == top);
        for (const auto& m : mids) {
            const glm::ivec2 sp = r.project_to_screen(m.x, m.y, m.z);
            px[on].push_back(GCodeLayerRendererTestAccess::cache_pixel(r, sp.x, sp.y));
        }
    }

    WallProbe p;
    for (size_t k = 0; k < mids.size(); ++k) {
        if ((px[0][k] >> 24) == 0) {
            continue;
        }
        ++p.visible;
        if (luma(px[1][k]) < luma(px[0][k])) {
            ++p.darker;
        } else if (luma(px[1][k]) > luma(px[0][k])) {
            ++p.lighter;
        }
    }
    return p;
}

/// Midpoints of the four edges of the square [lo, hi] at height z.
std::vector<glm::vec3> square_mids(float lo, float hi, float z) {
    const float mid = (lo + hi) * 0.5f;
    return {{mid, lo, z}, {hi, mid, z}, {mid, hi, z}, {lo, mid, z}};
}

WallProbe probe_layer(const ParsedGCodeFile& gcode, int top) {
    const float lo = top < 10 ? 20.0f : 15.0f;
    const float hi = top < 10 ? 40.0f : 45.0f;
    return probe_walls([&](GCodeLayerRenderer& r) { r.set_gcode(&gcode); }, top,
                       square_mids(lo, hi, gcode.layers[top].z_height));
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "crease: the cache build darkens creased walls only",
                 "[gcode][crease][layer_renderer]") {
    const auto gcode = make_stepped_tower(FeatureType::OuterWall);

    SECTION("a flat stretch of wall is untouched") {
        const WallProbe p = probe_layer(gcode, 5);
        REQUIRE(p.visible >= 2);
        REQUIRE(p.darker == 0);
        REQUIRE(p.lighter == 0);
    }
    SECTION("the overhang ledge darkens") {
        const WallProbe p = probe_layer(gcode, 10);
        REQUIRE(p.visible >= 2);
        REQUIRE(p.darker == p.visible);
    }
    SECTION("the rim the wall steps out from darkens") {
        const WallProbe p = probe_layer(gcode, 9);
        REQUIRE(p.visible >= 2);
        REQUIRE(p.darker == p.visible);
    }
}

TEST_CASE_METHOD(LVGLTestFixture, "crease: a file without wall types renders unchanged",
                 "[gcode][crease][layer_renderer]") {
    const auto gcode = make_stepped_tower(FeatureType::Unknown);
    const WallProbe p = probe_layer(gcode, 10);
    REQUIRE(p.visible >= 2);
    REQUIRE(p.darker == 0);
    REQUIRE(p.lighter == 0);
}

TEST_CASE_METHOD(LVGLTestFixture, "crease: wall indexes live only while the cache builds",
                 "[gcode][crease][layer_renderer]") {
    const auto gcode = make_stepped_tower(FeatureType::OuterWall);
    GCodeLayerRenderer r;
    r.set_gcode(&gcode);
    r.set_ghost_mode(false);
    r.set_canvas_size(kCanvasPx, kCanvasPx);
    r.auto_fit();

    // A partial batch leaves the neighbours of its last layer indexed, and the
    // memory report counts them.
    GCodeLayerRendererTestAccess::pin_layers_per_frame(r, 100);
    GCodeLayerRendererTestAccess::freeze_batch_clock(r);
    REQUIRE(GCodeLayerRendererTestAccess::render_solid_batch(r, 0, 5, kCanvasPx, kCanvasPx) == 5);
    REQUIRE(r.memory_report().bytes_for("crease_grids") > 0);

    // Once render() sees the cache complete, they are given back.
    lv_obj_t* canvas = lv_canvas_create(test_screen());
    static uint8_t buf[static_cast<size_t>(kCanvasPx) * kCanvasPx * 4];
    lv_canvas_set_buffer(canvas, buf, kCanvasPx, kCanvasPx, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_update_layout(canvas);
    r.set_current_layer(19);
    auto frame = [&]() {
        lv_layer_t layer;
        lv_area_t clip = {0, 0, kCanvasPx - 1, kCanvasPx - 1};
        lv_canvas_init_layer(canvas, &layer);
        r.render(&layer, &clip);
        lv_canvas_finish_layer(canvas, &layer);
    };
    for (int i = 0; i < 3; ++i) {
        frame();
    }
    int guard = 0;
    while (r.needs_more_frames() && guard++ < 500) {
        frame();
    }
    REQUIRE(guard < 500);
    REQUIRE(r.get_cached_up_to_layer() == 19);
    frame();
    REQUIRE(r.memory_report().bytes_for("crease_grids") == 0);
}

TEST_CASE_METHOD(LVGLTestFixture, "crease: a neighbour that fails to stream in darkens nothing",
                 "[gcode][crease][layer_renderer][streaming]") {
    std::vector<size_t> starts;
    TempGCode file(make_tower_text(10, starts));
    GCodeStreamingController controller;
    REQUIRE(controller.open_source(std::make_unique<FailingSource>(file.path(), starts[6], -1)));
    REQUIRE(controller.get_layer_count() == 10);
    // The layer drawn on top reads; the one above it comes back empty, which
    // is how the controller reports a failed read.
    const auto five = controller.get_layer_segments(5);
    REQUIRE(five);
    REQUIRE_FALSE(five->empty());
    const auto six = controller.get_layer_segments(6);
    REQUIRE((!six || six->empty()));
    REQUIRE(controller.get_layer_line_count(6) > 0);

    const WallProbe p =
        probe_walls([&](GCodeLayerRenderer& r) { r.set_streaming_controller(&controller); }, 5,
                    square_mids(20.0f, 40.0f, controller.get_layer_z(5)));
    REQUIRE(p.visible >= 2);
    REQUIRE(p.darker == 0);
}

TEST_CASE_METHOD(LVGLTestFixture, "crease: a streamed neighbour that loads late still darkens",
                 "[gcode][crease][layer_renderer][streaming]") {
    // The top layer of a readable file is a rim: proves the streamed path
    // reaches the crease at all, through the one-frame wait.
    std::vector<size_t> starts;
    TempGCode file(make_tower_text(10, starts));
    GCodeStreamingController controller;
    REQUIRE(controller.open_source(std::make_unique<FileDataSource>(file.path())));
    const WallProbe p =
        probe_walls([&](GCodeLayerRenderer& r) { r.set_streaming_controller(&controller); }, 9,
                    square_mids(20.0f, 40.0f, controller.get_layer_z(9)));
    REQUIRE(p.visible >= 2);
    REQUIRE(p.darker == p.visible);
}

TEST_CASE_METHOD(LVGLTestFixture, "crease: a sequential print's next object is not a ledge",
                 "[gcode][crease][layer_renderer]") {
    // Object A prints whole (layers 0-9, z 1-10), then object B starts back
    // at the bed (layers 10-14, z 1-5), right after A's top in file order.
    ParsedGCodeFile gcode;
    for (int i = 0; i < 15; ++i) {
        Layer layer;
        const bool b = i >= 10;
        layer.z_height = kLayerMm * static_cast<float>((b ? i - 10 : i) + 1);
        layer.segments =
            b ? square(60.0f, 80.0f, layer.z_height) : square(20.0f, 40.0f, layer.z_height);
        gcode.layers.push_back(std::move(layer));
    }
    const WallProbe p = probe_walls([&](GCodeLayerRenderer& r) { r.set_gcode(&gcode); }, 11,
                                    square_mids(60.0f, 80.0f, gcode.layers[11].z_height));
    REQUIRE(p.visible >= 2);
    REQUIRE(p.darker == 0);
}

TEST_CASE_METHOD(LVGLTestFixture, "crease: objects at different layer heights do not crease",
                 "[gcode][crease][layer_renderer]") {
    // A at 1mm layers, B at 1.5mm: z order 1.0 A, 1.5 B, 2.0 A, 3.0 A+B, ...
    // The layer under A's z=2.0 holds only B.
    struct L {
        float z;
        bool a, b;
    };
    const L plan[] = {{1.0f, true, false}, {1.5f, false, true}, {2.0f, true, false},
                      {3.0f, true, true},  {4.0f, true, false}, {4.5f, false, true},
                      {5.0f, true, false}};
    ParsedGCodeFile gcode;
    for (const auto& l : plan) {
        Layer layer;
        layer.z_height = l.z;
        if (l.a) {
            layer.segments = square(20.0f, 40.0f, l.z);
        }
        if (l.b) {
            const auto bs = square(60.0f, 80.0f, l.z);
            layer.segments.insert(layer.segments.end(), bs.begin(), bs.end());
        }
        gcode.layers.push_back(std::move(layer));
    }
    const WallProbe p = probe_walls([&](GCodeLayerRenderer& r) { r.set_gcode(&gcode); }, 2,
                                    square_mids(20.0f, 40.0f, 2.0f));
    REQUIRE(p.visible >= 2);
    REQUIRE(p.darker == 0);
}

TEST_CASE_METHOD(LVGLTestFixture, "crease: an evenly flaring spiral vase does not crease",
                 "[gcode][crease][layer_renderer]") {
    // Each layer is one climbing revolution: segment Z rises through the
    // layer's height, and the square grows 0.5mm per layer (a constant slope).
    ParsedGCodeFile gcode;
    std::vector<glm::vec3> top_mids;
    constexpr int kTop = 5;
    for (int i = 0; i < 10; ++i) {
        Layer layer;
        layer.z_height = kLayerMm * static_cast<float>(i + 1);
        const float lo = 20.0f - 0.5f * static_cast<float>(i);
        const float hi = 40.0f + 0.5f * static_cast<float>(i);
        const glm::vec2 c[4] = {{lo, lo}, {hi, lo}, {hi, hi}, {lo, hi}};
        for (int k = 0; k < 4; ++k) {
            const float z0 = layer.z_height + kLayerMm * static_cast<float>(k) / 4.0f;
            const float z1 = layer.z_height + kLayerMm * static_cast<float>(k + 1) / 4.0f;
            ToolpathSegment s = wall(c[k], c[(k + 1) % 4], 0.0f);
            s.start.z = z0;
            s.end.z = z1;
            layer.segments.push_back(s);
            if (i == kTop) {
                const glm::vec2 m = (c[k] + c[(k + 1) % 4]) * 0.5f;
                top_mids.emplace_back(m.x, m.y, (z0 + z1) * 0.5f);
            }
        }
        gcode.layers.push_back(std::move(layer));
    }
    const WallProbe p =
        probe_walls([&](GCodeLayerRenderer& r) { r.set_gcode(&gcode); }, kTop, top_mids);
    REQUIRE(p.visible >= 2);
    REQUIRE(p.darker == 0);
}

TEST_CASE_METHOD(LVGLTestFixture, "a solid-cache batch stops at the first layer past its budget",
                 "[gcode][layer_renderer][budget]") {
    const auto gcode = make_stepped_tower(FeatureType::OuterWall);
    GCodeLayerRenderer r;
    r.set_gcode(&gcode);
    r.set_ghost_mode(false);
    r.set_canvas_size(kCanvasPx, kCanvasPx);
    r.auto_fit();
    GCodeLayerRendererTestAccess::pin_layers_per_frame(r, 100);

    // Every clock read moves 3ms: the deadline is read at +0, layers 1 and 2
    // check in under the 8ms box, layer 3's check lands on +9.
    REQUIRE(GCodeLayerRendererTestAccess::solid_batch_budget_ms() == 8);
    GCodeLayerRendererTestAccess::step_batch_clock<3>(r);
    CHECK(GCodeLayerRendererTestAccess::render_solid_batch(r, 0, 19, kCanvasPx, kCanvasPx) == 2);

    // A stopped clock lets one batch take every layer it is handed.
    GCodeLayerRendererTestAccess::freeze_batch_clock(r);
    CHECK(GCodeLayerRendererTestAccess::render_solid_batch(r, 3, 19, kCanvasPx, kCanvasPx) == 19);
}

TEST_CASE_METHOD(LVGLTestFixture, "crease: toggling crease shading rebuilds the cache",
                 "[gcode][crease][layer_renderer]") {
    const auto gcode = make_stepped_tower(FeatureType::OuterWall);
    GCodeLayerRenderer r;
    r.set_gcode(&gcode);
    r.set_ghost_mode(false);
    r.set_canvas_size(kCanvasPx, kCanvasPx);
    r.auto_fit();
    GCodeLayerRendererTestAccess::pin_layers_per_frame(r, 100);
    GCodeLayerRendererTestAccess::freeze_batch_clock(r);
    REQUIRE(GCodeLayerRendererTestAccess::render_solid_batch(r, 0, 5, kCanvasPx, kCanvasPx) == 5);
    const glm::ivec2 sp = r.project_to_screen(30.0f, 20.0f, gcode.layers[5].z_height);
    REQUIRE((GCodeLayerRendererTestAccess::cache_pixel(r, sp.x, sp.y) >> 24) != 0);

    r.set_crease_shading(true); // unchanged: the cache stands
    REQUIRE((GCodeLayerRendererTestAccess::cache_pixel(r, sp.x, sp.y) >> 24) != 0);
    r.set_crease_shading(false);
    REQUIRE(GCodeLayerRendererTestAccess::cache_pixel(r, sp.x, sp.y) == 0);
}
