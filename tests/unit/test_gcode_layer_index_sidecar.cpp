// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_gcode_layer_index_sidecar.cpp
 * @brief GCodeLayerIndex built from the plugin's layer index sidecar.
 *
 * Run with: ./build/bin/helix-tests "[plugin_file_analysis][layer_index]"
 *
 * The sidecar fixture is what moonraker-plugin's analyzer writes for the
 * G-code fixture beside it (pinned by test_helix_analysis.py), so the index
 * built from it is compared with build_from_file() over the same G-code.
 */

#include "gcode_layer_index.h"
#include "gcode_streaming_controller.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

#include "../catch_amalgamated.hpp"

using Catch::Approx;
using helix::gcode::GCodeLayerIndex;

namespace {

constexpr const char* kGcode = "moonraker-plugin/tests/fixtures/analysis/orca_two_tool.gcode";
constexpr const char* kSidecar =
    "moonraker-plugin/tests/fixtures/analysis/orca_two_tool.layers.jsonl";

std::string slurp(const char* path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

uint64_t size_of(const std::string& text) {
    return static_cast<uint64_t>(text.size());
}

std::string hash() {
    return helix::gcode::layer_index_file_hash(kGcode);
}

} // namespace

TEST_CASE("an index built from the sidecar matches one built from the file",
          "[plugin_file_analysis][layer_index]") {
    const std::string gcode = slurp(kGcode);
    GCodeLayerIndex parsed;
    REQUIRE(parsed.build_from_file(kGcode));
    GCodeLayerIndex sidecar;
    REQUIRE(sidecar.build_from_sidecar(slurp(kSidecar), size_of(gcode), hash(), kGcode));

    REQUIRE(sidecar.get_layer_count() == parsed.get_layer_count());
    CHECK(sidecar.get_source_path() == kGcode);
    CHECK(sidecar.uses_layer_markers() == parsed.uses_layer_markers());
    for (size_t i = 0; i < parsed.get_layer_count(); ++i) {
        INFO("layer " << i);
        const auto p = parsed.get_entry(i);
        const auto s = sidecar.get_entry(i);
        REQUIRE(s.is_valid());
        CHECK(s.z_height == Approx(p.z_height));
        CHECK(s.start_x == Approx(p.start_x));
        CHECK(s.start_y == Approx(p.start_y));
        CHECK(s.start_z == Approx(p.start_z));
        CHECK(s.start_e == Approx(p.start_e).margin(1e-4));
        CHECK(s.flags == p.flags);
        CHECK(s.start_tool == p.start_tool);
        CHECK(s.start_feature_type == p.start_feature_type);
        // A sidecar layer opens at its marker, a few comment lines before the
        // first Z move the file scan opens it at, and both end where the next opens.
        CHECK(s.file_offset <= p.file_offset);
        CHECK(p.file_offset - s.file_offset < 80);
        const std::string between = gcode.substr(s.file_offset, p.file_offset - s.file_offset);
        CHECK(between.rfind(";LAYER_CHANGE", 0) == 0);
        CHECK(between.find("G1 X") == std::string::npos);
    }

    const auto& ps = parsed.get_stats();
    const auto& ss = sidecar.get_stats();
    CHECK(ss.total_bytes == ps.total_bytes);
    CHECK(ss.min_x == Approx(ps.min_x));
    CHECK(ss.max_x == Approx(ps.max_x));
    CHECK(ss.min_y == Approx(ps.min_y));
    CHECK(ss.max_y == Approx(ps.max_y));
    CHECK(ss.min_z == Approx(ps.min_z));
    CHECK(ss.max_z == Approx(ps.max_z));
    CHECK(ss.tools_used == ps.tools_used);
    CHECK(ss.initial_tool_index == ps.initial_tool_index);
    CHECK(ss.filament_palette == ps.filament_palette);
    CHECK(ss.filament_color == ps.filament_color);

    REQUIRE(ps.pause_scan);
    REQUIRE(ss.pause_scan);
    REQUIRE(ss.pause_scan->pauses.size() == 1);
    REQUIRE(ss.pause_scan->pauses.size() == ps.pause_scan->pauses.size());
    CHECK(ss.pause_scan->pauses[0].file_offset == ps.pause_scan->pauses[0].file_offset);
    CHECK(ss.pause_scan->pauses[0].layer_index == ps.pause_scan->pauses[0].layer_index);
    CHECK(ss.pause_scan->pauses[0].kind == ps.pause_scan->pauses[0].kind);
    CHECK(ss.pause_scan->pauses[0].slicer_fraction ==
          Approx(ps.pause_scan->pauses[0].slicer_fraction));
    CHECK(ss.pause_scan->axis == ps.pause_scan->axis);
    REQUIRE(ss.pause_scan->layer_marks.size() == ps.pause_scan->layer_marks.size());
    for (size_t i = 0; i < ps.pause_scan->layer_marks.size(); ++i) {
        CHECK(ss.pause_scan->layer_marks[i].layer == ps.pause_scan->layer_marks[i].layer);
        CHECK(ss.pause_scan->layer_marks[i].byte_fraction ==
              Approx(ps.pause_scan->layer_marks[i].byte_fraction));
    }
}

TEST_CASE("a sidecar for another file version builds nothing",
          "[plugin_file_analysis][layer_index]") {
    const std::string sidecar = slurp(kSidecar);
    const uint64_t size = size_of(slurp(kGcode));
    GCodeLayerIndex index;
    CHECK_FALSE(index.build_from_sidecar(sidecar, size + 1, hash(), kGcode));
    CHECK_FALSE(index.is_valid());
}

TEST_CASE("the file hash is the one the plugin records", "[plugin_file_analysis][layer_index]") {
    const std::string sidecar = slurp(kSidecar);
    const std::string recorded = "\"file_hash\":\"" + hash() + "\"";
    CHECK(hash().size() == 40);
    CHECK(sidecar.substr(0, sidecar.find('\n')).find(recorded) != std::string::npos);
    CHECK(helix::gcode::layer_index_file_hash("no/such/file.gcode").empty());
}

TEST_CASE("a sidecar for other bytes of the same size builds nothing",
          "[plugin_file_analysis][layer_index]") {
    const std::string sidecar = slurp(kSidecar);
    const uint64_t size = size_of(slurp(kGcode));
    GCodeLayerIndex index;
    CHECK_FALSE(index.build_from_sidecar(sidecar, size, std::string(40, '0'), kGcode));
    CHECK_FALSE(index.build_from_sidecar(sidecar, size, "", kGcode));
    CHECK_FALSE(index.is_valid());
}

TEST_CASE("an unfinished sidecar builds nothing", "[plugin_file_analysis][layer_index]") {
    std::string sidecar = slurp(kSidecar);
    const uint64_t size = size_of(slurp(kGcode));
    const auto last = sidecar.rfind("{\"complete\"");
    REQUIRE(last != std::string::npos);
    sidecar.erase(last);
    GCodeLayerIndex index;
    CHECK_FALSE(index.build_from_sidecar(sidecar, size, hash(), kGcode));
    CHECK(index.get_layer_count() == 0);
}

TEST_CASE("a sidecar line cut short is skipped", "[plugin_file_analysis][layer_index]") {
    std::string sidecar = slurp(kSidecar);
    const uint64_t size = size_of(slurp(kGcode));
    const auto second = sidecar.find('\n') + 1;
    sidecar.insert(second, "{\"layer\": 9, \"off\n");
    GCodeLayerIndex index;
    REQUIRE(index.build_from_sidecar(sidecar, size, hash(), kGcode));
    CHECK(index.get_layer_count() == 3);
}

TEST_CASE("garbage is not a sidecar", "[plugin_file_analysis][layer_index]") {
    GCodeLayerIndex index;
    CHECK_FALSE(index.build_from_sidecar("", 100, "abc", "x.gcode"));
    CHECK_FALSE(index.build_from_sidecar("not json\n{}\n", 100, "abc", "x.gcode"));
}

TEST_CASE("a streaming open takes its index from a matching sidecar",
          "[plugin_file_analysis][layer_index][streaming]") {
    helix::gcode::GCodeStreamingController controller;
    SECTION("matching: no pass over the file") {
        controller.set_layer_index_sidecar(slurp(kSidecar));
        REQUIRE(controller.open_file(kGcode));
        CHECK(controller.get_layer_count() == 3);
        CHECK(controller.get_index_stats().total_lines == 0);
    }
    SECTION("for another file: the pass runs") {
        std::string sidecar = slurp(kSidecar);
        const auto at = sidecar.find("\"file_size\":");
        REQUIRE(at != std::string::npos);
        sidecar.replace(at, 12, "\"file_size\":1");
        controller.set_layer_index_sidecar(sidecar);
        REQUIRE(controller.open_file(kGcode));
        CHECK(controller.get_layer_count() == 3);
        CHECK(controller.get_index_stats().total_lines > 0);
    }
    SECTION("an open consumes it") {
        controller.set_layer_index_sidecar(slurp(kSidecar));
        REQUIRE(controller.open_file(kGcode));
        REQUIRE(controller.open_file(kGcode));
        CHECK(controller.get_index_stats().total_lines > 0);
    }
}

TEST_CASE("layer records after the complete line are not part of the index",
          "[plugin_file_analysis][layer_index]") {
    std::string sidecar = slurp(kSidecar);
    const uint64_t size = size_of(slurp(kGcode));
    // A follower that appended after analysis renamed its index in repeats layers.
    std::string layers;
    size_t pos = sidecar.find('\n') + 1;
    while (true) {
        const size_t end = sidecar.find('\n', pos);
        if (end == std::string::npos) {
            break;
        }
        const std::string line = sidecar.substr(pos, end - pos + 1);
        if (line.rfind("{\"layer\"", 0) == 0) {
            layers += line;
        }
        pos = end + 1;
    }
    REQUIRE_FALSE(layers.empty());
    sidecar += layers;
    GCodeLayerIndex index;
    REQUIRE(index.build_from_sidecar(sidecar, size, hash(), kGcode));
    CHECK(index.get_layer_count() == 3);
}
