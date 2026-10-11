// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gcode_camera.h"

#include <cmath>
#include <glm/glm.hpp>

#include "../catch_amalgamated.hpp"

using namespace helix::gcode;
using Catch::Approx;

namespace {
// NDC -> pixel mapping with Y flipped, as the renderers use.
glm::vec2 to_screen(const GCodeCamera& cam, glm::vec3 p) {
    glm::vec4 clip = cam.get_view_projection_matrix() * glm::vec4(p, 1.0f);
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return {(ndc.x + 1.0f) * 0.5f * cam.get_viewport_width(),
            (1.0f - ndc.y) * 0.5f * cam.get_viewport_height()};
}

GCodeCamera make_camera(float zoom) {
    GCodeCamera cam;
    cam.set_viewport_size(800, 480);
    cam.set_zoom_level(zoom);
    return cam;
}
} // namespace

TEST_CASE("GCodeCamera::pan_pixels moves content exactly with the finger", "[gcode][camera]") {
    for (float zoom : {1.4f, 6.0f}) {
        auto cam = make_camera(zoom);
        const glm::vec3 p{12.0f, -7.0f, 3.0f};
        const glm::vec2 before = to_screen(cam, p);
        cam.pan_pixels(37.0f, -21.0f);
        const glm::vec2 after = to_screen(cam, p);
        CHECK(after.x - before.x == Approx(37.0f).margin(0.5));
        CHECK(after.y - before.y == Approx(-21.0f).margin(0.5));
    }
}

TEST_CASE("GCodeCamera::zoom_at keeps the world point under the anchor fixed", "[gcode][camera]") {
    auto cam = make_camera(1.4f);
    const glm::vec3 p{20.0f, 15.0f, 0.0f};
    const glm::vec2 anchor = to_screen(cam, p);
    cam.zoom_at(1.5f, anchor.x, anchor.y);
    cam.zoom_at(1.3f, anchor.x, anchor.y);
    const glm::vec2 after = to_screen(cam, p);
    CHECK(after.x == Approx(anchor.x).margin(0.5));
    CHECK(after.y == Approx(anchor.y).margin(0.5));
    CHECK(cam.get_zoom_level() == Approx(1.4f * 1.5f * 1.3f));
}

TEST_CASE("GCodeCamera::zoom_at at the zoom clamp does not drift the view", "[gcode][camera]") {
    auto cam = make_camera(100.0f); // at the max clamp
    const glm::vec3 target_before = cam.get_target();
    cam.zoom_at(1.3f, 50.0f, 60.0f);
    const glm::vec3 target_after = cam.get_target();
    CHECK(cam.get_zoom_level() == Approx(100.0f));
    CHECK(target_after.x == Approx(target_before.x).margin(1e-4));
    CHECK(target_after.y == Approx(target_before.y).margin(1e-4));
    CHECK(target_after.z == Approx(target_before.z).margin(1e-4));
}

TEST_CASE("GCodeCamera pan/zoom on a zero-height viewport leave the target finite",
          "[gcode][camera]") {
    GCodeCamera cam;
    cam.set_viewport_size(800, 0);
    cam.pan_pixels(10.0f, 10.0f);
    cam.zoom_at(1.2f, 10.0f, 10.0f);
    const glm::vec3 t = cam.get_target();
    CHECK(std::isfinite(t.x));
    CHECK(std::isfinite(t.y));
    CHECK(std::isfinite(t.z));
}
