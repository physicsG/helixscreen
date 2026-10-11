// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_start_macro_remap_note.cpp
 * @brief The Filament Mapping dialog's start-macro warning, from cached scan to text.
 *
 * Run with: ./build/bin/helix-tests "[remap][note]"
 *
 * The note is only honest when the start line it names came from the file
 * being remapped, the route is a file rewrite, and the line carries a tool
 * parameter the rewrite leaves. Each half is asked separately here:
 * PrintPreparationManager::print_start_for() answers "which line", and
 * PrintSelectPanel::start_macro_remap_note() answers "what does it say".
 */

#include "ui_panel_print_select.h"
#include "ui_print_preparation_manager.h"

#include "../lvgl_test_fixture.h"
#include "gcode_ops_detector.h"

#include <string>

#include "../catch_amalgamated.hpp"

using helix::AmsBackend;
using helix::gcode::PrintStartCallInfo;
using helix::gcode::ScanResult;
using helix::ui::PrintPreparationManager;

namespace {
ScanResult scan_with_start(bool found, const std::string& line) {
    ScanResult scan;
    scan.print_start.found = found;
    scan.print_start.macro_name = "PRINT_START";
    scan.print_start.raw_line = line;
    return scan;
}

constexpr const char* kLine = "PRINT_START INITIAL_TOOL=0 EXTRUDER_TEMP=150 EXTRUDER1_TEMP=0";
} // namespace

TEST_CASE("print_start_for answers only for the file the scan was built for", "[remap][note]") {
    PrintPreparationManager prep;
    CHECK(prep.print_start_for("benchy.gcode") == nullptr); // nothing scanned yet

    prep.set_cached_scan_result(scan_with_start(true, kLine), "benchy.gcode");
    const PrintStartCallInfo* start = prep.print_start_for("benchy.gcode");
    REQUIRE(start != nullptr);
    CHECK(start->raw_line == kLine);

    // Another file's start line says nothing about this one.
    CHECK(prep.print_start_for("cube.gcode") == nullptr);

    // A scan that found no start macro has no line to warn about.
    prep.set_cached_scan_result(scan_with_start(false, ""), "cube.gcode");
    CHECK(prep.print_start_for("cube.gcode") == nullptr);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "start_macro_remap_note names the macro and the params left alone",
                 "[remap][note]") {
    using S = AmsBackend::RemapStrategy;
    const PrintStartCallInfo start = scan_with_start(true, kLine).print_start;

    const std::string note = PrintSelectPanel::start_macro_remap_note(S::GcodeRewrite, &start);
    CHECK(note.find("PRINT_START") != std::string::npos);
    CHECK(note.find("(EXTRUDER_TEMP, EXTRUDER1_TEMP)") != std::string::npos);
    CHECK(note.find("INITIAL_TOOL") == std::string::npos);

    // No line for this file, a route that never rewrites, or nothing left: no note.
    CHECK(PrintSelectPanel::start_macro_remap_note(S::GcodeRewrite, nullptr).empty());
    CHECK(PrintSelectPanel::start_macro_remap_note(S::Native, &start).empty());
    CHECK(PrintSelectPanel::start_macro_remap_note(S::PrePrintSend, &start).empty());
    const PrintStartCallInfo clean = scan_with_start(true, "PRINT_START BED_TEMP=60").print_start;
    CHECK(PrintSelectPanel::start_macro_remap_note(S::GcodeRewrite, &clean).empty());
}
