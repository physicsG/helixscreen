// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gcode_tool_remapper.h"

#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#include "../catch_amalgamated.hpp"

static std::string slurp(const std::string& p) {
    std::ifstream f(p);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// Split into physical lines (drop the trailing empty element from a final newline).
static std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> lines;
    std::stringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }
    return lines;
}

TEST_CASE("U1 remap rewrites all three command families", "[remap][gcode]") {
    std::map<int, int> remap = {{1, 2}}; // logical tool 1 -> physical head 2
    std::string in = slurp("assets/test_gcodes/u1_4color_ring.gcode");
    REQUIRE(!in.empty()); // fixture found
    std::string out = helix::GcodeToolRemapper::apply_to_string(in, remap);

    // body Tn: no bare "T1" line remains; T0 lines untouched
    CHECK(out.find("\nT1\n") == std::string::npos);
    CHECK(out.find("\nT0\n") != std::string::npos);

    // prestart family: every EXECUTABLE SM_PRINT_* command line has been rewritten
    // away from EXTRUDER=1. As with the temp family, a global substring scan is NOT
    // a valid contract -- the fixture's "; machine_start_gcode = ..." comment line
    // embeds the literal "SM_PRINT_EXTRUDER_PREHEAT EXTRUDER=1" inside a comment,
    // and a conservative remapper must not touch comments. So we assert per command
    // line (lines that actually START with SM_PRINT_*, not comment text).
    bool saw_auto_feed_0 = false;
    for (const auto& line : split_lines(out)) {
        if (line.rfind("SM_PRINT_AUTO_FEED EXTRUDER=", 0) == 0) {
            CHECK(line.rfind("SM_PRINT_AUTO_FEED EXTRUDER=1", 0) != 0);
        }
        if (line.rfind("SM_PRINT_EXTRUDER_PREHEAT EXTRUDER=", 0) == 0) {
            CHECK(line.rfind("SM_PRINT_EXTRUDER_PREHEAT EXTRUDER=1", 0) != 0);
        }
        if (line.rfind("SM_PRINT_FLOW_CALIBRATE EXTRUDER=", 0) == 0) {
            CHECK(line.rfind("SM_PRINT_FLOW_CALIBRATE EXTRUDER=1", 0) != 0);
        }
        if (line.rfind("SM_PRINT_AUTO_FEED EXTRUDER=0", 0) == 0) {
            saw_auto_feed_0 = true; // head 0 untouched
        }
    }
    CHECK(saw_auto_feed_0);

    // temp family: no M104/M109 COMMAND line retains a "T1" tool token.
    // NOTE: a global out.find(" T1 ") scan is NOT a valid contract here -- the
    // captured fixture embeds tool tokens inside comments (e.g. the
    // "; machine_start_gcode = ... S0 T1 A0 ..." escaped block on one physical
    // line, and "M104 S220 T1 ; preheat T1 time: 30s" where the comment text
    // also contains "T1"). A conservative remapper must NOT rewrite comments,
    // so we assert the precise behavioral truth: the executable portion of every
    // M104/M109 line carries the remapped head, not the original.
    for (const auto& line : split_lines(out)) {
        if (line.rfind("M104", 0) != 0 && line.rfind("M109", 0) != 0) {
            continue;
        }
        std::string code = line.substr(0, line.find(';')); // strip trailing comment
        // strip trailing whitespace so " T1" at EOL is caught regardless of \r/spaces
        while (!code.empty() && std::isspace(static_cast<unsigned char>(code.back()))) {
            code.pop_back();
        }
        CHECK(code.find(" T1 ") == std::string::npos);
        if (code.size() >= 3) {
            CHECK(code.substr(code.size() - 3) != " T1"); // no tool token at end of command
        }
    }
    // and the remap target landed on at least one temp line
    CHECK(out.find("M109 S220 T2") != std::string::npos);
}

TEST_CASE("remap is collision-safe for a swap", "[remap][gcode]") {
    // each line mapped from its ORIGINAL index in a single pass: 1<->2 swap must not chain
    std::string in = "T1\nT2\nSM_PRINT_AUTO_FEED EXTRUDER=1\nSM_PRINT_AUTO_FEED EXTRUDER=2\n";
    std::map<int, int> remap = {{1, 2}, {2, 1}};
    std::string out = helix::GcodeToolRemapper::apply_to_string(in, remap);
    CHECK(out == "T2\nT1\nSM_PRINT_AUTO_FEED EXTRUDER=2\nSM_PRINT_AUTO_FEED EXTRUDER=1\n");
}

TEST_CASE("unmapped indices and unrelated lines are untouched", "[remap][gcode]") {
    std::string in = "T0\nT3\nG1 X10 Y10\nM104 S200 T0\n";
    std::map<int, int> remap = {{1, 2}}; // nothing matches
    CHECK(helix::GcodeToolRemapper::apply_to_string(in, remap) == in);
}

TEST_CASE("temp token remapped in all positions", "[remap][gcode]") {
    std::map<int, int> remap = {{1, 2}};
    // token mid-line, token at EOL, token before comment
    std::string in = "M104 T1 S140\n"
                     "M109 S220 T1\n"
                     "M104 S70 T1 ; set nozzle temperature ;cooldown\n";
    std::string out = helix::GcodeToolRemapper::apply_to_string(in, remap);
    CHECK(out == "M104 T2 S140\n"
                 "M109 S220 T2\n"
                 "M104 S70 T2 ; set nozzle temperature ;cooldown\n");
}

TEST_CASE("build_line_replacements emits only changed lines, 1-based", "[remap][gcode]") {
    std::map<int, int> remap = {{1, 2}};
    std::string in = "T0\nT1\nG1 X1\nSM_PRINT_AUTO_FEED EXTRUDER=1\n";
    auto reps = helix::GcodeToolRemapper::build_line_replacements(in, remap);
    REQUIRE(reps.size() == 2);
    CHECK(reps[0].line_number == 2);
    CHECK(reps[0].new_line == "T2");
    CHECK(reps[1].line_number == 4);
    CHECK(reps[1].new_line == "SM_PRINT_AUTO_FEED EXTRUDER=2");
}

TEST_CASE("comment lines containing tool tokens are not rewritten", "[remap][gcode]") {
    std::map<int, int> remap = {{1, 2}};
    std::string in = "; Change Tool1 -> Tool0\n"
                     "; machine_start_gcode = ...M104 S0 T1 A0...\n"
                     "G28\n";
    // none of these are bare-Tn / SM_PRINT_ / M10x command lines -> unchanged
    CHECK(helix::GcodeToolRemapper::apply_to_string(in, remap) == in);
}
