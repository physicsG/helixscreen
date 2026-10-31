// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layer_pause.cpp
 * @brief Tests for scheduling a pause into a running print (#1703).
 *
 * helix::layer_pause owns which macros offer the feature, the gcode each action
 * sends, where the schedule reads back from, and which reason the UI shows.
 * The printer reaches it only through its object list here, the way discovery
 * reaches it.
 */

#include "layer_pause.h"
#include "printer_discovery.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using helix::PrinterDiscovery;
using nlohmann::json;
namespace lp = helix::layer_pause;

namespace {

PrinterDiscovery printer_with(const json& objects) {
    PrinterDiscovery hw;
    hw.parse_objects(objects);
    return hw;
}

/// The client.cfg macro set Mainsail, Fluidd and the U1's firmware all ship.
json client_cfg_objects() {
    return json::array({"toolhead", "print_stats", "gcode_macro PAUSE", "gcode_macro RESUME",
                        "gcode_macro SET_PAUSE_NEXT_LAYER", "gcode_macro SET_PAUSE_AT_LAYER",
                        "gcode_macro SET_PRINT_STATS_INFO"});
}

json state(bool next, bool at, int layer) {
    return {{"pause_next_layer", {{"enable", next}, {"call", "PAUSE"}}},
            {"pause_at_layer", {{"enable", at}, {"layer", layer}, {"call", "PAUSE"}}}};
}

} // namespace

TEST_CASE("layer_pause: offered only when both macros exist", "[layer_pause]") {
    CHECK(lp::supported(printer_with(client_cfg_objects())));
    CHECK_FALSE(lp::supported(printer_with(json::array({"toolhead", "gcode_macro PAUSE"}))));
    CHECK_FALSE(lp::supported(
        printer_with(json::array({"gcode_macro SET_PAUSE_NEXT_LAYER", "gcode_macro PAUSE"}))));
    CHECK_FALSE(lp::supported(
        printer_with(json::array({"gcode_macro SET_PAUSE_AT_LAYER", "gcode_macro PAUSE"}))));
}

TEST_CASE("layer_pause: subscribes to the wrapper as printer.cfg spells it", "[layer_pause]") {
    SECTION("standard spelling") {
        auto objs = lp::required_status_objects(printer_with(client_cfg_objects()));
        REQUIRE(objs.size() == 1);
        CHECK(objs[0] == "gcode_macro SET_PRINT_STATS_INFO");
    }
    SECTION("config-case spelling is kept verbatim") {
        auto objs = lp::required_status_objects(printer_with(
            json::array({"gcode_macro set_pause_next_layer", "gcode_macro SET_PAUSE_AT_LAYER",
                         "gcode_macro Set_Print_Stats_Info"})));
        REQUIRE(objs.size() == 1);
        CHECK(objs[0] == "gcode_macro Set_Print_Stats_Info");
    }
    SECTION("no wrapper means nothing to read back") {
        auto hw = printer_with(
            json::array({"gcode_macro SET_PAUSE_NEXT_LAYER", "gcode_macro SET_PAUSE_AT_LAYER"}));
        CHECK(lp::supported(hw));
        CHECK(lp::required_status_objects(hw).empty());
        CHECK(lp::state_object(hw).empty());
    }
    SECTION("unsupported printer subscribes to nothing, even with the wrapper") {
        CHECK(lp::required_status_objects(
                  printer_with(json::array({"gcode_macro SET_PRINT_STATS_INFO"})))
                  .empty());
    }
}

TEST_CASE("layer_pause: commands leave the configured pause call alone", "[layer_pause]") {
    CHECK(lp::schedule_next_layer_command() == "SET_PAUSE_NEXT_LAYER ENABLE=1");
    CHECK(lp::cancel_next_layer_command() == "SET_PAUSE_NEXT_LAYER ENABLE=0");
    CHECK(lp::schedule_at_layer_command(80) == "SET_PAUSE_AT_LAYER ENABLE=1 LAYER=80");
    CHECK(lp::cancel_at_layer_command() == "SET_PAUSE_AT_LAYER ENABLE=0");
}

TEST_CASE("layer_pause: parse_state reads both variables", "[layer_pause]") {
    const lp::Schedule none;

    SECTION("full object") {
        auto s = lp::parse_state(state(true, true, 42), none);
        REQUIRE(s);
        CHECK(s->next_layer);
        CHECK(s->at_layer == 42);
    }
    SECTION("disabled at_layer keeps no layer, whatever the stale number") {
        auto s = lp::parse_state(state(false, false, 42), none);
        REQUIRE(s);
        CHECK_FALSE(s->any());
    }
    SECTION("enable may arrive as a number") {
        json obj = {{"pause_at_layer", {{"enable", 1}, {"layer", 9}}}};
        auto s = lp::parse_state(obj, none);
        REQUIRE(s);
        CHECK(s->at_layer == 9);
    }
    SECTION("a delta frame keeps the variable it omits") {
        lp::Schedule prev;
        prev.next_layer = true;
        json delta = {{"pause_at_layer", {{"enable", true}, {"layer", 5}}}};
        auto s = lp::parse_state(delta, prev);
        REQUIRE(s);
        CHECK(s->next_layer);
        CHECK(s->at_layer == 5);
    }
    SECTION("an object with neither variable is no read-back") {
        CHECK_FALSE(lp::parse_state(json{{"other", 1}}, none).has_value());
        CHECK_FALSE(lp::parse_state(json::array(), none).has_value());
        CHECK_FALSE(lp::parse_state(json{{"pause_at_layer", 3}}, none).has_value());
    }
}

TEST_CASE("layer_pause: pending_layer", "[layer_pause]") {
    lp::Schedule s;
    CHECK(lp::pending_layer(s, 10) == 0);
    s.at_layer = 20;
    CHECK(lp::pending_layer(s, 10) == 20);
    CHECK(lp::pending_layer(s, 20) == 0); // reached: fired or never will
    CHECK(lp::pending_layer(s, 25) == 0);
    s.next_layer = true;
    CHECK(lp::pending_layer(s, 10) == 11); // the nearer pause wins
}

TEST_CASE("layer_pause: tracker without read-back", "[layer_pause]") {
    lp::Tracker t;
    CHECK_FALSE(t.has_readback());

    SECTION("next layer expires once the layer advances") {
        t.note_next_layer(true, 5);
        CHECK(t.effective().next_layer);
        CHECK_FALSE(t.on_layer(5));
        CHECK(t.effective().next_layer);
        CHECK(t.on_layer(6));
        CHECK_FALSE(t.effective().next_layer);
    }
    SECTION("at layer expires on reaching it") {
        t.note_at_layer(8);
        CHECK_FALSE(t.on_layer(7));
        CHECK(t.effective().at_layer == 8);
        CHECK(t.on_layer(8));
        CHECK(t.effective().at_layer == 0);
    }
    SECTION("cancel clears") {
        t.note_at_layer(8);
        t.note_at_layer(0);
        CHECK_FALSE(t.effective().any());
        t.note_next_layer(true, 1);
        t.note_next_layer(false, 1);
        CHECK_FALSE(t.effective().any());
    }
    SECTION("a new print forgets what was sent") {
        t.note_at_layer(8);
        t.reset_for_new_print();
        CHECK_FALSE(t.effective().any());
    }
}

TEST_CASE("layer_pause: tracker with read-back", "[layer_pause]") {
    lp::Tracker t;
    CHECK(t.apply_status(state(false, true, 30)));
    CHECK(t.has_readback());
    CHECK(t.effective().at_layer == 30);

    SECTION("the macros report their own firing; layers do not expire it") {
        CHECK_FALSE(t.on_layer(31));
        CHECK(t.effective().at_layer == 30);
        CHECK(t.apply_status(state(false, false, 30)));
        CHECK(t.effective().at_layer == 0);
    }
    SECTION("a sent command shows at once") {
        t.note_next_layer(true, 3);
        CHECK(t.effective().next_layer);
        t.note_at_layer(0);
        CHECK(t.effective().at_layer == 0);
    }
    SECTION("read-back outlives a new print, as the macro state does") {
        t.reset_for_new_print();
        CHECK(t.effective().at_layer == 30);
    }
    SECTION("an unchanged frame reports no change") {
        CHECK_FALSE(t.apply_status(state(false, true, 30)));
    }
    SECTION("a frame without the variables is ignored") {
        CHECK_FALSE(t.apply_status(json{{"unrelated", 1}}));
        CHECK(t.effective().at_layer == 30);
    }
}

TEST_CASE("layer_pause: availability reasons in order", "[layer_pause]") {
    using A = lp::Availability;
    CHECK(lp::classify(false, true, 3, 100) == A::Hidden);
    CHECK(lp::classify(true, false, 3, 100) == A::NoLayerData);
    CHECK(lp::classify(true, true, 0, 0) == A::NoLayerData);
    CHECK(lp::classify(true, true, 100, 100) == A::LastLayer);
    CHECK(lp::classify(true, true, 99, 100) == A::Ready);
    CHECK(lp::classify(true, true, 0, 100) == A::Ready);
}

TEST_CASE("layer_pause: picker bounds are (current, total]", "[layer_pause]") {
    auto b = lp::layer_bounds(10, 100);
    CHECK(b.min_layer == 11);
    CHECK(b.max_layer == 100);
    b = lp::layer_bounds(0, 5);
    CHECK(b.min_layer == 1);
    CHECK(b.max_layer == 5);
    b = lp::layer_bounds(100, 100); // never inverted
    CHECK(b.min_layer == 101);
    CHECK(b.max_layer == 101);
}

TEST_CASE("layer_pause: the pause that just happened expires what was sent", "[layer_pause]") {
    lp::Tracker t;
    SECTION("next layer") {
        t.note_next_layer(true, 4);
        CHECK(t.on_paused(4));
        CHECK_FALSE(t.effective().any());
    }
    SECTION("the layer after the one still reported") {
        t.note_at_layer(5);
        CHECK(t.on_paused(4));
        CHECK(t.effective().at_layer == 0);
    }
    SECTION("a pause well before the chosen layer keeps it") {
        t.note_at_layer(9);
        CHECK_FALSE(t.on_paused(4));
        CHECK(t.effective().at_layer == 9);
    }
    SECTION("reset forgets read-back too") {
        t.apply_status({{"pause_at_layer", {{"enable", true}, {"layer", 9}}}});
        REQUIRE(t.has_readback());
        t.reset();
        CHECK_FALSE(t.has_readback());
        CHECK_FALSE(t.effective().any());
    }
}
