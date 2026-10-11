// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "../test_fixtures.h"
#include "../test_helpers/plugin_test_support.h"
#include "connection_state.h"
#include "lua_bindings.h"

#include "../catch_amalgamated.hpp"

using namespace helix::plugin;
using namespace helix::plugin::test;

TEST_CASE_METHOD(XMLTestFixture, "printer.get maps the stable names",
                 "[plugin][bindings][printer]") {
    BoundRuntime b({&install_printer_bindings});
    lv_subject_set_int(lv_xml_get_subject(nullptr, "bed_temp"), 605);
    lv_subject_copy_string(lv_xml_get_subject(nullptr, "print_state"), "printing");
    lv_subject_set_int(lv_xml_get_subject(nullptr, "printer_connection_state"),
                       static_cast<int>(helix::ConnectionState::CONNECTED));
    REQUIRE(b.t.run(R"(
        bed = helix.printer.get("bed_temp")
        state = helix.printer.get("print_state")
        conn = helix.printer.get("connected")
    )"));
    CHECK(b.t.global("bed") == "60.5");
    CHECK(b.t.global("state") == "printing");
    CHECK(b.t.global("conn") == "true");
}

TEST_CASE_METHOD(XMLTestFixture, "printer.get refuses names outside the table",
                 "[plugin][bindings][printer]") {
    BoundRuntime b({&install_printer_bindings});
    CHECK_FALSE(b.t.run(R"(helix.printer.get("print_state_enum"))"));
    CHECK_FALSE(b.t.run(R"(helix.printer.get("chamber_effective_target"))"));
}

TEST_CASE_METHOD(XMLTestFixture, "printer.watch reports changes in Lua units",
                 "[plugin][bindings][printer]") {
    BoundRuntime b({&install_printer_bindings});
    REQUIRE(b.t.run(R"(
        seen = {}
        helix.printer.watch("extruder_target", function(v) seen[#seen + 1] = v end)
    )"));
    lv_subject_set_int(lv_xml_get_subject(nullptr, "extruder_target"), 2150);
    REQUIRE(b.t.run("r = table.concat(seen, ',')"));
    CHECK(b.t.global("r") == "215.0");
}

TEST_CASE_METHOD(XMLTestFixture, "subject observers and printer watches share one quota",
                 "[plugin][bindings][printer]") {
    BoundRuntime b({&install_ui_bindings, &install_printer_bindings});
    REQUIRE(b.t.run(R"(
        local s = helix.subject.int("n", 0)
        for i = 1, 255 do s:observe(function() end) end
        helix.printer.watch("bed_temp", function() end)
        ok1, e1 = pcall(s.observe, s, function() end)
        ok2, e2 = pcall(helix.printer.watch, "bed_temp", function() end)
    )"));
    CHECK(b.t.global("ok1") == "false");
    CHECK(b.t.global("e1").find("at most 256") != std::string::npos);
    CHECK(b.t.global("ok2") == "false");
    CHECK(b.t.global("e2").find("at most 256") != std::string::npos);
}

TEST_CASE_METHOD(XMLTestFixture, "printer watchers detach when the runtime closes",
                 "[plugin][bindings][printer]") {
    {
        BoundRuntime b({&install_printer_bindings});
        REQUIRE(b.t.run(R"(helix.printer.watch("bed_temp", function() end))"));
    }
    lv_subject_set_int(lv_xml_get_subject(nullptr, "bed_temp"), 1);
    SUCCEED(); // ASAN is what proves the closed runtime was not called
}

TEST_CASE("the stable printer table", "[plugin][bindings][printer]") {
    REQUIRE(printer_fields().size() == 10);
    CHECK(std::string(printer_fields()[0].lua_name) == "connected");
}

#endif // HELIX_HAS_PLUGINS
