// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Task 12: verify the production apply path used by
// PrintPreparationManager::modify_and_print_with_remap().
//
// The prep manager does NOT call GcodeToolRemapper::apply_to_string directly on
// the file. Instead it asks the remapper for only the CHANGED lines
// (build_line_replacements) and feeds each one as a single-line REPLACE
// modification into GCodeFileModifier. This test proves that conversion is
// faithful: REPLACE-by-line modifications built from build_line_replacements
// reproduce apply_to_string (the oracle) exactly, line for line.

#include "gcode_file_modifier.h"
#include "gcode_tool_remapper.h"

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

static std::string slurp_remap_fixture(const std::string& p) {
    std::ifstream f(p);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

static std::vector<std::string> to_lines(const std::string& s) {
    std::vector<std::string> lines;
    std::stringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }
    // Drop trailing empty lines. apply_to_string and GCodeFileModifier's
    // re-serializer differ only in how many trailing blank lines they preserve
    // (the fixture ends in "\n\n\n"); that boundary is irrelevant to remap
    // correctness, which is about CONTENT of the rewritten command lines.
    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    return lines;
}

// Mirror the prep-manager conversion: build_line_replacements -> REPLACE mods.
static std::string apply_via_modifier(const std::string& content, const std::map<int, int>& remap) {
    auto replacements = helix::GcodeToolRemapper::build_line_replacements(content, remap);
    helix::gcode::GCodeFileModifier modifier;
    for (const auto& r : replacements) {
        modifier.add_modification(helix::gcode::Modification::replace(
            static_cast<size_t>(r.line_number), r.new_line, "tool remap"));
    }
    return modifier.apply_to_content(content);
}

TEST_CASE("Remap apply: REPLACE-by-line matches apply_to_string oracle", "[remap][apply]") {
    std::map<int, int> remap = {{1, 2}}; // logical tool 1 -> physical head 2
    std::string in = slurp_remap_fixture("assets/test_gcodes/u1_4color_ring.gcode");
    REQUIRE(!in.empty()); // fixture present

    std::string oracle = helix::GcodeToolRemapper::apply_to_string(in, remap);
    std::string produced = apply_via_modifier(in, remap);

    // Compare line-by-line so a trailing-newline difference between the two
    // serializers is not a false failure — the production path's correctness is
    // about line CONTENT, not the final byte.
    auto oracle_lines = to_lines(oracle);
    auto produced_lines = to_lines(produced);

    REQUIRE(produced_lines.size() == oracle_lines.size());
    for (size_t i = 0; i < oracle_lines.size(); ++i) {
        INFO("line " << (i + 1));
        CHECK(produced_lines[i] == oracle_lines[i]);
    }
}

TEST_CASE("Remap apply: identity remap produces no replacements", "[remap][apply]") {
    std::string in = slurp_remap_fixture("assets/test_gcodes/u1_4color_ring.gcode");
    REQUIRE(!in.empty());

    // Mapping a tool to itself must not rewrite any line — the prep manager
    // relies on an empty replacement list to short-circuit to a direct print.
    std::map<int, int> identity = {{1, 1}};
    auto replacements = helix::GcodeToolRemapper::build_line_replacements(in, identity);
    CHECK(replacements.empty());
}

TEST_CASE("Remap apply: swap is collision-safe via single-pass replacements", "[remap][apply]") {
    std::map<int, int> swap = {{0, 1}, {1, 0}};
    std::string in = slurp_remap_fixture("assets/test_gcodes/u1_4color_ring.gcode");
    REQUIRE(!in.empty());

    std::string oracle = helix::GcodeToolRemapper::apply_to_string(in, swap);
    std::string produced = apply_via_modifier(in, swap);

    auto oracle_lines = to_lines(oracle);
    auto produced_lines = to_lines(produced);
    REQUIRE(produced_lines.size() == oracle_lines.size());
    for (size_t i = 0; i < oracle_lines.size(); ++i) {
        INFO("line " << (i + 1));
        CHECK(produced_lines[i] == oracle_lines[i]);
    }
}
