// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_gcode_offscreen_reveal.cpp
 * @brief A 2D preview must be able to build while its widget is hidden.
 *
 * The print preview stacks a thumbnail over the viewer and swaps them when the
 * render has real content. For the viewer to be genuinely hidden until then -
 * rather than drawn underneath an opaque thumbnail, which costs a full canvas
 * blit per frame for pixels nobody sees - the ghost build has to make progress
 * without a draw pass.
 *
 * render() is what starts the ghost thread and copies its result onto the
 * canvas, so a hidden widget that waited on a draw would never start, never
 * finish, and never reveal. pump_offscreen_build() is the same three steps
 * driven from a timer instead.
 */

#include "../lvgl_test_fixture.h"
#include "gcode_layer_renderer.h"
#include "gcode_parser.h"

#include <glm/glm.hpp>

#include "../catch_amalgamated.hpp"

using namespace helix::gcode;

namespace {

constexpr int kCanvas = 64;
constexpr int kLayers = 3;

ParsedGCodeFile make_small_tower() {
    ParsedGCodeFile gcode;
    const int16_t obj = gcode.intern_object_name("box");

    for (int i = 0; i < kLayers; ++i) {
        Layer layer;
        const float z = 0.2f * static_cast<float>(i + 1);
        layer.z_height = z;

        const glm::vec3 c[4] = {
            {20.0f, 20.0f, z}, {40.0f, 20.0f, z}, {40.0f, 40.0f, z}, {20.0f, 40.0f, z}};
        for (int k = 0; k < 4; ++k) {
            ToolpathSegment seg;
            seg.start = c[k];
            seg.end = c[(k + 1) % 4];
            seg.is_extrusion = true;
            seg.object_name_index = obj;
            layer.segments.push_back(seg);
        }
        gcode.layers.push_back(std::move(layer));
    }
    return gcode;
}

/// Drive the pump the way the preview panel's timer does, with a bound so a
/// regression fails the test instead of hanging the suite.
bool pump_until_revealed(GCodeLayerRenderer& r, int max_ticks = 2000) {
    for (int i = 0; i < max_ticks; ++i) {
        if (r.pump_offscreen_build(kCanvas, kCanvas)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "2D preview reaches reveal with no draw pass",
                 "[gcode][ghost][preview]") {
    GCodeLayerRenderer renderer;
    auto gcode = make_small_tower();
    renderer.set_gcode(&gcode);
    renderer.set_canvas_size(kCanvas, kCanvas);

    // Nothing has drawn this renderer, and nothing will.
    REQUIRE_FALSE(renderer.has_first_output());

    REQUIRE(pump_until_revealed(renderer));
    CHECK(renderer.has_first_output());
}

TEST_CASE_METHOD(LVGLTestFixture, "pump is idempotent once revealed", "[gcode][ghost][preview]") {
    GCodeLayerRenderer renderer;
    auto gcode = make_small_tower();
    renderer.set_gcode(&gcode);
    renderer.set_canvas_size(kCanvas, kCanvas);

    REQUIRE(pump_until_revealed(renderer));

    // The panel keeps ticking until it acts on the reveal; further pumps must
    // not restart the build or drop the result.
    for (int i = 0; i < 5; ++i) {
        CHECK(renderer.pump_offscreen_build(kCanvas, kCanvas));
    }
    CHECK(renderer.has_first_output());
}

TEST_CASE_METHOD(LVGLTestFixture, "pump settles immediately for an empty model",
                 "[gcode][ghost][preview]") {
    GCodeLayerRenderer renderer;
    ParsedGCodeFile empty;
    renderer.set_gcode(&empty);
    renderer.set_canvas_size(kCanvas, kCanvas);

    // No layers means no worker to spawn and nothing to wait for, so the gate
    // opens on the first tick. The caller must not sit in a pump loop forever
    // on a model that will never produce pixels.
    CHECK(renderer.pump_offscreen_build(kCanvas, kCanvas));
    CHECK_FALSE(renderer.is_ghost_build_running());
}
