// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_afc_fault_position.cpp
 * @brief The AFC fault-message -> filament-position contract (#1184).
 *
 * Run with: ./build/bin/helix-tests "[afc][fault][position]"
 *
 * Every message below is VERBATIM from the AFC-Klipper-Add-On source read off a live
 * BoxTurtle — the five (and only five) sites that ship a position diagram:
 *
 *   AFC.py:1294, AFC.py:1345, AFC.py:1370, AFC.py:1469, AFC_BoxTurtle.py:527
 *
 * The art is a hardcoded literal per site, so we map the SENTENCE, never the bars.
 * The two toolhead-sensor sites emit byte-identical art for different faults — that
 * disambiguation is the whole reason this module exists, and it is asserted below.
 *
 * The contract is documented in
 * `docs/devel/FILAMENT_MANAGEMENT.md` § "AFC console response contract".
 *
 * Mutation checks (each must break the listed test):
 *   - collapse the pre/post extruder-gear rules onto one segment
 *     -> "identical art, different positions" fails
 *   - make afc_strip_position_diagram() strip unconditionally (drop the
 *     afc_fault_position() gate) -> "an unrecognised message is returned untouched" fails
 *   - drop the word-boundary check in contains_word()
 *     -> "unrelated console text is not a fault position" fails on the filename line
 */

#include "afc_fault_position.h"

#include <optional>
#include <string>

#include "../catch_amalgamated.hpp"

using helix::afc::afc_fault_position;
using helix::afc::afc_strip_position_diagram;

namespace {

// ---------------------------------------------------------------------------
// The five producers, verbatim (Python literals rendered with real newlines).
// ---------------------------------------------------------------------------

// AFC.py:1294
const std::string MSG_HUB = "filament did not trigger hub sensor, CHECK FILAMENT PATH\n"
                            "||=====||==>--||-----||\n"
                            "TRG   LOAD   HUB   TOOL.";

// AFC.py:1345
const std::string MSG_PRE_GEAR =
    "filament failed to trigger pre extruder gear toolhead sensor, CHECK FILAMENT PATH\n"
    "||=====||====||==>--||\n"
    "TRG   LOAD   HUB   TOOL";

// AFC.py:1370
const std::string MSG_POST_GEAR =
    "filament failed to trigger post extruder gear toolhead sensor, CHECK FILAMENT PATH\n"
    "||=====||====||==>--||\n"
    "TRG   LOAD   HUB   TOOL";

// AFC.py:1469
const std::string MSG_NOT_LOADED = "Current lane not loaded, LOAD TRIGGER NOT TRIGGERED\n"
                                   "||==>--||----||-----||\n"
                                   "TRG   LOAD   HUB   TOOL";

// AFC_BoxTurtle.py:527
const std::string MSG_TRIGGER = " FAILED TO LOAD, CHECK FILAMENT AT TRIGGER\n"
                                "||==>--||----||------||\n"
                                "TRG   LOAD   HUB    TOOL";

/// As it really arrives: AFC prefixes the lane name onto the sentence.
const std::string MSG_PRE_GEAR_WITH_LANE =
    "lane1 filament failed to trigger pre extruder gear toolhead sensor, CHECK FILAMENT PATH\n"
    "||=====||====||==>--||\n"
    "TRG   LOAD   HUB   TOOL";

} // namespace

// ============================================================================
// Mapping
// ============================================================================

TEST_CASE("every AFC diagram-bearing message maps to a position", "[afc][fault][position]") {
    struct Case {
        const char* name;
        const std::string* message;
        helix::PathSegment expected;
    };

    // Table-driven over all five real producers plus the lane-prefixed form.
    const Case cases[] = {
        {"AFC.py:1469 lane not loaded", &MSG_NOT_LOADED, helix::PathSegment::SPOOL},
        {"AFC_BoxTurtle.py:527 failed at trigger", &MSG_TRIGGER, helix::PathSegment::SPOOL},
        {"AFC.py:1294 no hub sensor", &MSG_HUB, helix::PathSegment::HUB},
        {"AFC.py:1345 pre extruder gear", &MSG_PRE_GEAR, helix::PathSegment::OUTPUT},
        {"AFC.py:1370 post extruder gear", &MSG_POST_GEAR, helix::PathSegment::TOOLHEAD},
        {"AFC.py:1345 with lane prefix", &MSG_PRE_GEAR_WITH_LANE, helix::PathSegment::OUTPUT},
    };

    for (const auto& c : cases) {
        INFO(c.name);
        auto got = afc_fault_position(*c.message);
        REQUIRE(got.has_value());
        REQUIRE(static_cast<int>(*got) == static_cast<int>(c.expected));
    }
}

TEST_CASE("identical art, different positions", "[afc][fault][position]") {
    // Both sites emit the byte-identical bar row `||=====||====||==>--||`. A parser
    // over the art cannot tell them apart; the sentence can, and must.
    REQUIRE(MSG_PRE_GEAR.find("||=====||====||==>--||") != std::string::npos);
    REQUIRE(MSG_POST_GEAR.find("||=====||====||==>--||") != std::string::npos);

    auto pre = afc_fault_position(MSG_PRE_GEAR);
    auto post = afc_fault_position(MSG_POST_GEAR);
    REQUIRE(pre.has_value());
    REQUIRE(post.has_value());
    REQUIRE(static_cast<int>(*pre) != static_cast<int>(*post));
    REQUIRE(*pre == helix::PathSegment::OUTPUT);    // past hub, short of the toolhead
    REQUIRE(*post == helix::PathSegment::TOOLHEAD); // at toolhead, short of the gears
}

TEST_CASE("the `!!` prefix and case do not change the position", "[afc][fault][position]") {
    REQUIRE(afc_fault_position("!! lane1 filament did not trigger hub sensor, CHECK FILAMENT PATH")
                .value() == helix::PathSegment::HUB);
    REQUIRE(afc_fault_position("LANE1 FILAMENT DID NOT TRIGGER HUB SENSOR").value() ==
            helix::PathSegment::HUB);
    REQUIRE(
        afc_fault_position("lane1 Current lane not loaded, load trigger not triggered").value() ==
        helix::PathSegment::SPOOL);
}

TEST_CASE("unrelated console text is not a fault position", "[afc][fault][position]") {
    // The bare console channel carries user-controlled filenames and open-ended
    // chatter; none of it may resolve to a position.
    const char* noise[] = {
        "",
        "lane1 is now loaded in toolhead t:0",
        "Tool Change - lane1 -> lane2",
        "B:60.0 /60.0 T0:215.1 /215.0",
        "File opened: check filament at triggering.gcode Size: 91234",
        "// AFC_Brush: Clean Nozzle",
        "!! Extruder temperature 21.0C below minimum extrude temperature 170.0C",
        "hub sensor triggered",
        // Word-boundary guard: the needle only counts as whole words.
        "reload trigger not triggeredness detected",
    };

    for (const char* line : noise) {
        INFO(line);
        REQUIRE_FALSE(afc_fault_position(line).has_value());
    }
}

// ============================================================================
// Stripping
// ============================================================================

TEST_CASE("the art rows are stripped and the sentence survives byte-for-byte",
          "[afc][fault][position]") {
    REQUIRE(afc_strip_position_diagram(MSG_HUB) ==
            "filament did not trigger hub sensor, CHECK FILAMENT PATH");
    REQUIRE(afc_strip_position_diagram(MSG_PRE_GEAR) ==
            "filament failed to trigger pre extruder gear toolhead sensor, CHECK FILAMENT PATH");
    REQUIRE(afc_strip_position_diagram(MSG_POST_GEAR) ==
            "filament failed to trigger post extruder gear toolhead sensor, CHECK FILAMENT PATH");
    REQUIRE(afc_strip_position_diagram(MSG_NOT_LOADED) ==
            "Current lane not loaded, LOAD TRIGGER NOT TRIGGERED");
    // Leading space preserved exactly as AFC_BoxTurtle.py emits it.
    REQUIRE(afc_strip_position_diagram(MSG_TRIGGER) ==
            " FAILED TO LOAD, CHECK FILAMENT AT TRIGGER");
    REQUIRE(afc_strip_position_diagram(MSG_PRE_GEAR_WITH_LANE) ==
            "lane1 filament failed to trigger pre extruder gear toolhead sensor, CHECK "
            "FILAMENT PATH");

    // No stray newline is left where the art used to be.
    for (const std::string* m : {&MSG_HUB, &MSG_PRE_GEAR, &MSG_POST_GEAR, &MSG_NOT_LOADED,
                                 &MSG_TRIGGER, &MSG_PRE_GEAR_WITH_LANE}) {
        INFO(*m);
        REQUIRE(afc_strip_position_diagram(*m).find('\n') == std::string::npos);
    }
}

TEST_CASE("an unrecognised message is returned untouched", "[afc][fault][position]") {
    // The safety property: anything we did not recognise is not ours to edit, even
    // when it happens to contain something art-shaped.
    const char* untouched[] = {
        "!! Move out of range: 300.000 0.000 10.000 [0.000]",
        "Printer is shutdown\n||=====||====||==>--||\nTRG   LOAD   HUB   TOOL",
        "some future AFC wording we have never seen\n||==>--||----||-----||",
        "",
        "a plain sentence",
    };

    for (const char* line : untouched) {
        INFO(line);
        REQUIRE(afc_strip_position_diagram(line) == std::string(line));
    }
}

TEST_CASE("stripping never eats a sentence that merely mentions the labels",
          "[afc][fault][position]") {
    // A recognised fault whose sentence contains the label words must keep them.
    const std::string msg = "lane1 filament did not trigger hub sensor, CHECK FILAMENT PATH\n"
                            "TRG is the trigger, LOAD is the lane, HUB is the hub, TOOL is the "
                            "toolhead\n"
                            "||=====||==>--||-----||\n"
                            "TRG   LOAD   HUB   TOOL.";
    const std::string out = afc_strip_position_diagram(msg);
    REQUIRE(out == "lane1 filament did not trigger hub sensor, CHECK FILAMENT PATH\n"
                   "TRG is the trigger, LOAD is the lane, HUB is the hub, TOOL is the toolhead");
}

TEST_CASE("CRLF art rows are still recognised", "[afc][fault][position]") {
    const std::string msg = "filament did not trigger hub sensor, CHECK FILAMENT PATH\r\n"
                            "||=====||==>--||-----||\r\n"
                            "TRG   LOAD   HUB   TOOL.";
    REQUIRE(afc_strip_position_diagram(msg) ==
            "filament did not trigger hub sensor, CHECK FILAMENT PATH\r");
}
