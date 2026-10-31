// SPDX-License-Identifier: GPL-3.0-or-later

#include "start_block_steps.h"

#include <fstream>
#include <iterator>
#include <string>

#include "../catch_amalgamated.hpp"

using helix::scan_start_block;
using helix::StartBlockFollower;
using helix::StartBlockStep;
using Kind = helix::StartBlockStepKind;

namespace {

std::string fixture(const std::string& name) {
    std::string src = __FILE__;
    const auto pos = src.rfind("/tests/unit/");
    const std::string dir =
        pos != std::string::npos ? src.substr(0, pos) + "/tests/fixtures/" : "tests/fixtures/";
    std::ifstream f(dir + name, std::ios::binary);
    REQUIRE(f.is_open());
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

/// The U1 start block of tests/fixtures/u1_bmcu_start.gcode at its real byte
/// offsets: the file's first 12744 bytes (header, thumbnails, object list) are
/// blank lines here.
std::string u1_bmcu_head() {
    return std::string(12744, '\n') + fixture("u1_bmcu_start.gcode");
}

} // namespace

TEST_CASE("start block: every step of a real U1 start block, at its byte",
          "[start_block][snapmaker]") {
    const auto steps = scan_start_block(u1_bmcu_head(), false);
    struct Want {
        uint64_t offset;
        Kind kind;
        bool runs_on_its_line;
    };
    const Want want[] = {
        {12961, Kind::HomeAxes, true},     // G28 X Y
        {13756, Kind::CleanNozzle, false}, // ROUGHLY_CLEAN_NOZZLE_WITH_DISCARD
        {13824, Kind::ProbeZ, true},       // G28 Z I140 J140
        {14002, Kind::CleanNozzle, false}, // ROUGHLY_CLEAN_NOZZLE
        {14057, Kind::CleanNozzle, false}, // FINELY_CLEAN_NOZZLE_STAGE_1
        {14142, Kind::CleanNozzle, false}, // ROUGHLY_CLEAN_NOZZLE
        {14197, Kind::CleanNozzle, false}, // FINELY_CLEAN_NOZZLE_STAGE_2
        {14316, Kind::ProbeZ, true},       // G28 Z
        {14559, Kind::BedMesh, true},      // BED_MESH_CALIBRATE ... ADAPTIVE=0
        {14840, Kind::Purge, false},       // G1 X110 E15 F360, the prime line
    };
    REQUIRE(steps.size() == std::size(want));
    for (size_t i = 0; i < steps.size(); ++i) {
        CAPTURE(i, steps[i].offset);
        CHECK(steps[i].offset == want[i].offset);
        CHECK(steps[i].kind == want[i].kind);
        CHECK(steps[i].runs_on_its_line == want[i].runs_on_its_line);
    }
}

TEST_CASE("start block: what a line runs decides its step", "[start_block]") {
    const auto kinds = [](const std::string& head) {
        std::vector<Kind> out;
        for (const auto& step : scan_start_block(head, false)) {
            out.push_back(step.kind);
        }
        return out;
    };

    SECTION("G28 naming Z alone probes Z; any other homes the axes") {
        CHECK(kinds("G28\n") == std::vector{Kind::HomeAxes});
        CHECK(kinds("G28 X Y Z\n") == std::vector{Kind::HomeAxes});
        CHECK(kinds("g28 z ; touch\n") == std::vector{Kind::ProbeZ});
        CHECK(kinds("G28 Z0 I140 J140\n") == std::vector{Kind::ProbeZ});
    }
    SECTION("a stored mesh loaded or cleared is no calibration") {
        CHECK(kinds("BED_MESH_PROFILE LOAD=default\nBED_MESH_CLEAR\n").empty());
        CHECK(kinds("G29\n") == std::vector{Kind::BedMesh});
    }
    SECTION("a chamber soak is no step: temperature detection owns SOAKING") {
        CHECK(kinds("HEAT_SOAK\nG28\n") == std::vector{Kind::HomeAxes});
        CHECK(kinds("SET_HEATER_TEMPERATURE HEATER=chamber TARGET=50\n").empty());
    }
    SECTION("a keyword in a comment or a later token runs nothing") {
        CHECK(kinds("M109 S260 ; before CLEAN_NOZZLE\n").empty());
        CHECK(kinds("; G1 X10 E5\nG1 X5 F600 ; E5\n").empty());
        CHECK(kinds("G1 E-1.5 F1800\n").empty());
    }
    SECTION("each step on its own line, the first extrusion once") {
        CHECK(kinds("CLEAN_NOZZLE\nCLEAN_NOZZLE\nG1 X1 E2\nG1 X2 E2\n") ==
              std::vector{Kind::CleanNozzle, Kind::CleanNozzle, Kind::Purge});
    }
    SECTION("the block ends at the first layer") {
        CHECK(kinds("G28\n;LAYER_CHANGE\nCLEAN_NOZZLE\nG1 X1 E2\n") == std::vector{Kind::HomeAxes});
    }
    SECTION("CRLF lines keep their real offsets") {
        const auto steps = scan_start_block("M104 S0\r\nG28 Z\r\nCLEAN_NOZZLE\r\n", false);
        REQUIRE(steps.size() == 2);
        CHECK(steps[0].offset == 9);
        CHECK(steps[0].kind == Kind::ProbeZ);
        CHECK(steps[0].command == "G28");
        CHECK(steps[1].offset == 16);
        CHECK(steps[1].command == "CLEAN_NOZZLE");
    }
    SECTION("a read cut mid-line drops that line") {
        const auto steps = scan_start_block("G28\nBED_MESH_CALIBR", true);
        REQUIRE(steps.size() == 1);
        CHECK(steps[0].kind == Kind::HomeAxes);
    }
}

TEST_CASE("start block: the follower names each step the position reaches once", "[start_block]") {
    StartBlockFollower follower;
    follower.set_steps({{100, Kind::HomeAxes, true, ""},
                        {200, Kind::CleanNozzle, false, ""},
                        {300, Kind::ProbeZ, true, ""},
                        {400, Kind::BedMesh, true, ""},
                        {500, Kind::Purge, false, ""}});

    CHECK_FALSE(follower.advance(50));
    // A command that blocks shows while the position stands on its line.
    auto step = follower.advance(100);
    REQUIRE(step);
    CHECK(step->kind == Kind::HomeAxes);
    CHECK_FALSE(follower.advance(100));
    // A clean is queued moves: it runs while the position stands past it.
    step = follower.advance(250);
    REQUIRE(step);
    CHECK(step->kind == Kind::CleanNozzle);
    CHECK_FALSE(follower.advance(260));
    // The Z probe ran, then the position jumped past the mesh line without
    // standing on it: the printer skipped the mesh, so nothing new shows.
    step = follower.advance(300);
    REQUIRE(step);
    CHECK(step->kind == Kind::ProbeZ);
    CHECK_FALSE(follower.advance(450));
    // And the skipped mesh never shows later.
    step = follower.advance(510);
    REQUIRE(step);
    CHECK(step->kind == Kind::Purge);
    CHECK_FALSE(follower.advance(400));
}

TEST_CASE("start block: a position past several steps names the last", "[start_block]") {
    StartBlockFollower follower;
    follower.set_steps({{100, Kind::HomeAxes, true, ""},
                        {200, Kind::CleanNozzle, false, ""},
                        {300, Kind::CleanNozzle, false, ""},
                        {400, Kind::Purge, false, ""}});
    const auto step = follower.advance(350);
    REQUIRE(step);
    CHECK(step->offset == 300);
    CHECK(follower.advance(400)->kind == Kind::Purge);
}
