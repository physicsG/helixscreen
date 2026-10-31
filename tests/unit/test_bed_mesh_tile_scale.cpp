// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_bed_mesh_tile_scale.cpp
 * @brief The tile's colour scale bar is the heatmap's palette: its gradient stops in
 * panel_widget_bed_mesh_tile.xml equal what the renderer paints at the same positions.
 *
 * Run with: ./build/bin/helix-tests "[bed_mesh_tile][scale]"
 */

#include "bed_mesh_gradient.h"
#include "bed_mesh_renderer.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

#if HELIX_HAS_BED_MESH_3D

namespace {

struct Stop {
    uint32_t rgb;
    int offset; ///< 0..255
};

std::vector<Stop> scale_stops() {
    std::ifstream f("ui_xml/components/panel_widget_bed_mesh_tile.xml");
    REQUIRE(f.is_open()); // helix-tests runs from the repo root
    const std::string xml((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    const size_t start = xml.find("<horizontal name=\"bm_scale\">");
    REQUIRE(start != std::string::npos);
    const size_t end = xml.find("</horizontal>", start);
    REQUIRE(end != std::string::npos);

    std::vector<Stop> stops;
    for (size_t pos = xml.find("<stop ", start); pos != std::string::npos && pos < end;
         pos = xml.find("<stop ", pos + 1)) {
        const size_t color = xml.find("color=\"#", pos);
        const size_t offset = xml.find("offset=\"", pos);
        REQUIRE(color != std::string::npos);
        REQUIRE(offset != std::string::npos);
        stops.push_back({static_cast<uint32_t>(std::strtoul(xml.c_str() + color + 8, nullptr, 16)),
                         std::atoi(xml.c_str() + offset + 8)});
    }
    return stops;
}

} // namespace

TEST_CASE("bed_mesh_tile scale: the XML gradient stops are the renderer's palette",
          "[bed_mesh_tile][scale]") {
    const auto stops = scale_stops();
    REQUIRE(stops.size() == 5);

    // The renderer compresses the colour range around the mesh centre; the scale shows the
    // whole palette, so the stop at position p is sampled where the renderer reads p.
    constexpr double kMin = 0.0;
    constexpr double kMax = 1.0;
    const double span = (kMax - kMin) * BED_MESH_COLOR_COMPRESSION;
    const double low = (kMin + kMax) / 2.0 - span / 2.0;

    for (const Stop& stop : stops) {
        const double position = static_cast<double>(stop.offset) / 255.0;
        const lv_color_t painted =
            bed_mesh_gradient_height_to_color(low + position * span, kMin, kMax);
        const int r = static_cast<int>((stop.rgb >> 16) & 0xFF);
        const int g = static_cast<int>((stop.rgb >> 8) & 0xFF);
        const int b = static_cast<int>(stop.rgb & 0xFF);
        INFO("offset " << stop.offset);
        CHECK(std::abs(painted.red - r) <= 4);
        CHECK(std::abs(painted.green - g) <= 4);
        CHECK(std::abs(painted.blue - b) <= 4);
    }
}

#endif // HELIX_HAS_BED_MESH_3D
