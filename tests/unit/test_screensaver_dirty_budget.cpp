// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_ENABLE_SCREENSAVER

#include "../test_helpers/screensaver_test_access.h"
#include "screensaver_frame.h"
#include "screensaver_pixel_writer.h"
#include "screensaver_starfield.h"
#include "screensaver_starfield_sim.h"

#include <cstdint>
#include <random>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::ui::DirtyRect;
using helix::ui::FrameTarget;
using helix::ui::StarfieldSim;
using StarAccess = StarfieldScreensaverTestAccess;

namespace {

// The panel a two-core board drives, so coverage is a fraction of the real canvas.
constexpr uint32_t PANEL_W = 800;
constexpr uint32_t PANEL_H = 480;
constexpr int64_t PANEL_PX = static_cast<int64_t>(PANEL_W) * PANEL_H;

/**
 * @brief What one frame costs a two-core board, as a share of a core
 *
 * Solved from three K1C runs of the load gate at fixed star counts, rendering inline. A
 * frame's cost is dominated by the NUMBER of areas it invalidates: an area costs about
 * 3600x what one of its pixels costs, because each is a separate draw task and flush.
 * Rough by construction - it exists to catch a saver that grows an order of magnitude,
 * not to predict a percentage.
 */
constexpr double COST_FIXED = 10.1;
constexpr double COST_PER_AREA = 0.244;
constexpr double COST_PER_PX = 6.7e-5;

/// A saver gets a quarter of a core on a two-core board while the printer is idle.
constexpr double TWO_CORE_BUDGET = 25.0;

struct Load {
    double areas = 0;
    double pixels = 0;
    double cost() const {
        return COST_FIXED + COST_PER_AREA * areas + COST_PER_PX * pixels;
    }
};

/// Flies `stars` for `frames` and averages what each frame asked to have redrawn.
Load measure(int stars, int frames) {
    std::vector<uint8_t> bytes(static_cast<size_t>(PANEL_W) * PANEL_H * 2, 0);
    FrameTarget target{bytes.data(), PANEL_W * 2, PANEL_W, PANEL_H, helix::ui::PixelFormat::RGB565};
    std::minstd_rand rng(20260916);
    StarfieldSim sim;
    sim.init(PANEL_W, PANEL_H, rng);
    sim.set_active_count(stars);

    std::vector<DirtyRect> dirty;
    Load load;
    for (int f = 0; f < frames; f++) {
        sim.step(33, target, rng, dirty);
        load.areas += static_cast<double>(dirty.size());
        for (const DirtyRect& r : dirty) {
            load.pixels += static_cast<double>(r.area());
        }
    }
    load.areas /= frames;
    load.pixels /= frames;
    return load;
}

} // namespace

// A saver that reports one box per moving object keeps a frame's redraw proportional to the
// objects that moved. Reporting one box spanning them all instead puts the whole canvas in
// every frame, which is what the coverage figures here are guarding.
TEST_CASE("a starfield frame redraws a sliver of the canvas at every rung",
          "[screensaver][starfield][perf]") {
    StarfieldScreensaver saver;
    const size_t rungs = StarAccess::ladder_size(saver);
    REQUIRE(rungs >= 2);

    for (size_t level = 0; level < rungs; level++) {
        CAPTURE(level);
        const auto stars = static_cast<int>(StarAccess::star_count(saver, level));
        REQUIRE(stars > 0);

        const Load load = measure(stars, 240);

        // One box per star that moved, and never more boxes than LVGL will hold: past that
        // it drops every pending area and invalidates the screen, which costs a full redraw.
        CHECK(load.areas <= static_cast<double>(stars));
        CHECK(load.areas <= static_cast<double>(helix::ui::SAVER_MAX_DIRTY_AREAS));

        // Coverage well under the whole-canvas promotion, so a frame stays partial.
        const double coverage = 100.0 * load.pixels / static_cast<double>(PANEL_PX);
        CAPTURE(coverage);
        CHECK(coverage < 5.0);
        CHECK(coverage * 100.0 <
              static_cast<double>(helix::ui::SAVER_WHOLE_CANVAS_PERCENT) * 100.0);
    }
}

// The ladder exists so a board that cannot afford the top rung has somewhere to go. If its
// last rung does not fit a two-core budget, the gate runs out of rungs and blanks the screen.
TEST_CASE("the starfield's lowest rung fits a two-core board", "[screensaver][starfield][perf]") {
    StarfieldScreensaver saver;
    const size_t last = StarAccess::ladder_size(saver) - 1;
    const auto stars = static_cast<int>(StarAccess::star_count(saver, last));

    const Load load = measure(stars, 240);
    CAPTURE(stars, load.areas, load.pixels);
    CHECK(load.cost() < TWO_CORE_BUDGET);

    // And the rungs are ordered: each costs less than the one above it.
    double previous = 0.0;
    for (size_t level = 0; level <= last; level++) {
        const Load rung = measure(static_cast<int>(StarAccess::star_count(saver, level)), 120);
        CAPTURE(level, rung.areas);
        if (level > 0) {
            CHECK(rung.cost() < previous);
        }
        previous = rung.cost();
    }
}

#endif // HELIX_ENABLE_SCREENSAVER
