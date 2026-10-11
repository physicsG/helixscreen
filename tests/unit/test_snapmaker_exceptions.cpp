// tests/unit/test_snapmaker_exceptions.cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "snapmaker_exceptions.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using namespace helix::snapmaker;

TEST_CASE("a four-part code decodes as level-id-index-code", "[snapmaker][exceptions]") {
    auto c = decode_exception_code("0003-0530-0000-0011");
    REQUIRE(c.has_value());
    REQUIRE(c->level == 3);
    REQUIRE(c->id == 530);
    REQUIRE(c->index == 0);
    REQUIRE(c->code == 11);
}

TEST_CASE("a code embedded in a sentence is found", "[snapmaker][exceptions]") {
    auto c = decode_exception_code("!! Error 0003-0530-0000-0011 The plate has not been removed");
    REQUIRE(c.has_value());
    REQUIRE(c->id == 530);
}

TEST_CASE("text carrying no code decodes to nothing", "[snapmaker][exceptions]") {
    REQUIRE_FALSE(decode_exception_code("!! Must home Z axis first").has_value());
    REQUIRE_FALSE(decode_exception_code("").has_value());
}

TEST_CASE("a three-part code is not mistaken for a four-part one", "[snapmaker][exceptions]") {
    // The firmware also uses a 3-part basic form (id-index-code) with no level.
    // Reading it as level-id-index would silently shift every field.
    REQUIRE_FALSE(decode_exception_code("0530-0000-0011").has_value());
}

TEST_CASE("the plate-removal code maps to its message", "[snapmaker][exceptions]") {
    auto c = decode_exception_code("0003-0530-0000-0011");
    REQUIRE(exception_message(*c) ==
            "Remove the PEI sheet from the bed, then start again: probing through the sheet "
            "gives wrong results");
}

TEST_CASE("the power-loss code maps to its message", "[snapmaker][exceptions]") {
    // The firmware raises this one through invoke_shutdown rather than
    // exception_manager; without wording it would surface as an unrecognised
    // shutdown instead of naming the power loss.
    const auto c = decode_exception_code("0003-0522-0000-0017");
    REQUIRE(c.has_value());
    REQUIRE(exception_message(*c) ==
            "Power was lost during the print; the printer stopped and saved its progress");
}

TEST_CASE("the mid-print preference guard maps to its message", "[snapmaker][exceptions]") {
    const auto c = decode_exception_code("0002-0531-0000-0016");
    REQUIRE(c.has_value());
    REQUIRE(exception_message(*c) == "That setting cannot be changed while a print is running");
}

TEST_CASE("a code followed by a fifth group is not ours", "[snapmaker][exceptions]") {
    // A fifth dash-group means the payload is not level-id-index-code, so even
    // a window whose four groups all parse must be left alone.
    REQUIRE_FALSE(decode_exception_code("0003-0530-0000-0011-abcd").has_value());
}

TEST_CASE("an unknown code has no message rather than a wrong one", "[snapmaker][exceptions]") {
    // A wrong-but-confident message is worse than falling back to the
    // firmware's own text.
    ExceptionCode unknown{3, 999, 0, 0};
    REQUIRE(exception_message(unknown).empty());
}

TEST_CASE("levels map to what the firmware will do", "[snapmaker][exceptions]") {
    REQUIRE(severity_of(1) == ExceptionSeverity::Informational);
    REQUIRE(severity_of(2) == ExceptionSeverity::Pause);
    REQUIRE(severity_of(3) == ExceptionSeverity::Cancel);
    REQUIRE(severity_of(99) == ExceptionSeverity::Cancel); // unknown: assume the worst
}

TEST_CASE("active exceptions read out of a status frame", "[snapmaker][exceptions]") {
    nlohmann::json s = nlohmann::json::object();
    s["exception_manager"] = {{"exceptions",
                               {{{"id", 530},
                                 {"index", 0},
                                 {"code", 11},
                                 {"level", 3},
                                 {"message", "The plate has not been removed"},
                                 {"is_persistent", 0}}}}};
    auto v = read_active_exceptions(s);
    REQUIRE(v.size() == 1);
    REQUIRE(v[0].code.id == 530);
    // The message is the firmware's own, untranslated; wording for the screen
    // is the render path's job (faultcodes::classify).
    REQUIRE(v[0].message == "The plate has not been removed");
    REQUIRE(v[0].persistent == false);
}

TEST_CASE("an empty exception list means no active faults", "[snapmaker][exceptions]") {
    nlohmann::json s = nlohmann::json::object();
    s["exception_manager"] = {{"exceptions", nlohmann::json::array()}};
    REQUIRE(read_active_exceptions(s).empty());
}

TEST_CASE("a frame without exception_manager is silent, not empty", "[snapmaker][exceptions]") {
    // Distinguishing these matters: "no faults" clears a banner, "the frame did
    // not mention faults" must leave it alone.
    nlohmann::json s = nlohmann::json::object();
    s["toolhead"] = {{"homed_axes", "xyz"}};
    REQUIRE_FALSE(status_carries_exceptions(s));
    REQUIRE(status_carries_exceptions(
        nlohmann::json{{"exception_manager", {{"exceptions", nlohmann::json::array()}}}}));
}

TEST_CASE("every standing fault is read, not just the first", "[snapmaker][exceptions]") {
    // The list is read entry by entry; the array is never flattened into one
    // payload, which would lose everything after the first fault.
    nlohmann::json s = nlohmann::json::object();
    s["exception_manager"] = {{"exceptions",
                               {{{"id", 530},
                                 {"index", 0},
                                 {"code", 11},
                                 {"level", 3},
                                 {"message", "The plate has not been removed"},
                                 {"is_persistent", false}},
                                {{"id", 531},
                                 {"index", 0},
                                 {"code", 16},
                                 {"level", 2},
                                 {"message", "Cannot change while printing"},
                                 {"is_persistent", 1}}}}};
    auto v = read_active_exceptions(s);
    REQUIRE(v.size() == 2);
    REQUIRE(v[1].code.id == 531);
    REQUIRE(v[1].code.level == 2);
    REQUIRE(v[1].persistent == true);
}

TEST_CASE("a fault keeps the firmware's message, level or not", "[snapmaker][exceptions]") {
    // The entry also omits `level`: the firmware raises such faults at level 3
    // (cancel), and a missing level reads 0, which maps to Cancel as well.
    nlohmann::json s = nlohmann::json::object();
    s["exception_manager"] = {{"exceptions",
                               {{{"id", 999},
                                 {"index", 0},
                                 {"code", 1},
                                 {"message", "Firmware's own words"},
                                 {"is_persistent", true}}}}};
    auto v = read_active_exceptions(s);
    REQUIRE(v.size() == 1);
    REQUIRE(v[0].message == "Firmware's own words");
    REQUIRE(v[0].code.level == 0);
    REQUIRE(severity_of(v[0].code.level) == ExceptionSeverity::Cancel);
    REQUIRE(v[0].persistent == true);
}

TEST_CASE("a numeric field arriving as a string reads as unset, never parsed",
          "[snapmaker][exceptions]") {
    // A payload's shape is data, not a contract violation to throw on: the
    // field is skipped, and the firmware's own message stands regardless of
    // what code the entry carries.
    nlohmann::json s = nlohmann::json::object();
    s["exception_manager"] = {{"exceptions",
                               {{{"id", 530},
                                 {"index", 0},
                                 {"code", "0011"},
                                 {"level", 3},
                                 {"message", "whatever"},
                                 {"is_persistent", 0}}}}};
    auto v = read_active_exceptions(s);
    REQUIRE(v.size() == 1);
    REQUIRE(v[0].code.code == 0);
    REQUIRE(v[0].message == "whatever");
}
