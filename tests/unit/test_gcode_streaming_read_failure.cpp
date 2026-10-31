// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_gcode_streaming_read_failure.cpp
 * @brief A streamed layer whose read fails is retried, then skipped.
 *
 * The streaming controller reports a failed read as an empty layer, not a
 * null one. Drawn as it stands, that leaves a permanent gap in the solid
 * cache; retried forever, an unreadable layer would stall the build.
 */

#include "../lvgl_test_fixture.h"
#include "../test_helpers/gcode_layer_renderer_test_access.h"
#include "../test_helpers/streaming_test_sources.h"
#include "gcode_layer_renderer.h"
#include "gcode_streaming_controller.h"

#include <memory>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::gcode;
using namespace helix::gcode::test;

namespace {

constexpr int kPx = 240;
constexpr int kLayers = 10;
constexpr int kBad = 6;

/// Batches until the build reaches `top` or `max_batches` run out; returns
/// the last layer reached.
int build_to(GCodeLayerRenderer& r, int top, int max_batches) {
    int done = -1;
    for (int i = 0; i < max_batches && done < top; ++i) {
        done = GCodeLayerRendererTestAccess::render_solid_batch(r, done + 1, top, kPx, kPx);
    }
    return done;
}

void configure(GCodeLayerRenderer& r, GCodeStreamingController& c) {
    r.set_streaming_controller(&c);
    r.set_ghost_mode(false);
    r.set_canvas_size(kPx, kPx);
    r.auto_fit();
    GCodeLayerRendererTestAccess::pin_layers_per_frame(r, 100);
}

/// The cache pixels under layer `kBad`'s wall midpoints, drawn on top.
std::vector<uint32_t> bad_layer_pixels(GCodeLayerRenderer& r, float z) {
    std::vector<uint32_t> px;
    for (const glm::vec2 m :
         {glm::vec2(30, 20), glm::vec2(40, 30), glm::vec2(30, 40), glm::vec2(20, 30)}) {
        const glm::ivec2 s = r.project_to_screen(m.x, m.y, z);
        px.push_back(GCodeLayerRendererTestAccess::cache_pixel(r, s.x, s.y));
    }
    return px;
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "streaming: a layer whose read fails once is drawn on retry",
                 "[gcode][streaming][layer_renderer]") {
    std::vector<size_t> starts;
    TempGCode file(make_tower_text(kLayers, starts));

    GCodeStreamingController good;
    REQUIRE(good.open_source(std::make_unique<FileDataSource>(file.path())));
    GCodeLayerRenderer reference;
    configure(reference, good);
    REQUIRE(build_to(reference, kBad, 8) == kBad);
    const auto want = bad_layer_pixels(reference, good.get_layer_z(kBad));
    int painted = 0;
    for (uint32_t p : want) {
        painted += (p >> 24) != 0 ? 1 : 0;
    }
    REQUIRE(painted >= 2); // the probe lands on the layer's walls

    // Only the first read at or past layer 6's bytes fails.
    GCodeStreamingController flaky;
    REQUIRE(flaky.open_source(std::make_unique<FailingSource>(file.path(), starts[kBad], 1)));
    GCodeLayerRenderer r;
    configure(r, flaky);
    REQUIRE(build_to(r, kBad, 8) == kBad);
    REQUIRE(bad_layer_pixels(r, flaky.get_layer_z(kBad)) == want);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "streaming: an unreadable layer is skipped, not waited on forever",
                 "[gcode][streaming][layer_renderer]") {
    std::vector<size_t> starts;
    TempGCode file(make_tower_text(kLayers, starts));
    // Every read of layer 6's bytes fails, and so does every later layer's.
    GCodeStreamingController broken;
    REQUIRE(broken.open_source(std::make_unique<FailingSource>(file.path(), starts[kBad], -1)));
    GCodeLayerRenderer r;
    configure(r, broken);

    // Retried a bounded number of frames per layer, then passed over.
    REQUIRE(build_to(r, kLayers - 1, 64) == kLayers - 1);
}
