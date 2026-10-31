// SPDX-License-Identifier: GPL-3.0-or-later

// multiACE's `ace` status object and gcode vocabulary, read by the pure
// multiace_status module. Fixtures in tests/fixtures/multiace/ follow the
// engine's get_status() shape (multiACE 1.27b, api_version 1).

#include "multiace_status.h"
#include "printer_discovery.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using namespace helix;
using namespace helix::multiace;
using json = nlohmann::json;

namespace {

std::string fixture_path(const std::string& name) {
    std::string src = __FILE__;
    auto pos = src.rfind("/tests/unit/");
    const std::string root = pos != std::string::npos ? src.substr(0, pos) + "/" : "";
    return root + "tests/fixtures/multiace/" + name;
}

json load_fixture(const std::string& name) {
    std::ifstream f(fixture_path(name));
    INFO("fixture missing or unreadable: " << fixture_path(name));
    REQUIRE(f.is_open());
    json j;
    f >> j;
    return j;
}

PrinterDiscovery discover(const std::vector<std::string>& objects) {
    PrinterDiscovery hw;
    hw.parse_objects(json(objects));
    return hw;
}

constexpr std::array<HeadFeed, kHeadCount> kAllAce{HeadFeed::ACE, HeadFeed::ACE, HeadFeed::ACE,
                                                   HeadFeed::ACE};

} // namespace

TEST_CASE("multiace present needs ace and filament_detect", "[multiace]") {
    const std::vector<std::string> u1{"filament_detect", "extruder",  "extruder1",
                                      "extruder2",       "extruder3", "print_task_config",
                                      "toolhead"};

    auto with = [&u1](const std::string& extra) {
        auto objects = u1;
        objects.push_back(extra);
        return objects;
    };

    CHECK(present(discover(with("ace"))));
    CHECK_FALSE(present(discover(u1)));
    CHECK_FALSE(present(discover({"ace", "extruder", "heater_bed"})));
    CHECK_FALSE(present(discover(with("filament_hub"))));
    CHECK_FALSE(present(discover(with("ace_instance_0"))));
    CHECK_FALSE(present(discover(with("ace_device"))));
}

TEST_CASE("parse reads string head keys and drops anything else", "[multiace]") {
    const json ace = {{"head_ace", {{"0", 2}, {"1", 3}, {"2", "x"}, {"7", 1}, {"03", 1}}},
                      {"head_feeder", {{"0", true}, {"3", 1}, {"head", true}}},
                      {"head_manual", {{"1", true}}}};
    const StatusDelta d = parse_ace_status(ace);

    REQUIRE(d.head_ace);
    // A wrong-typed value leaves the head on the engine's own default, its index.
    CHECK(*d.head_ace == std::array<int, 4>{2, 3, 2, 3});
    REQUIRE(d.head_feeder);
    CHECK(*d.head_feeder == std::array<bool, 4>{true, false, false, false});
    REQUIRE(d.head_manual);
    CHECK(*d.head_manual == std::array<bool, 4>{false, true, false, false});
}

TEST_CASE("parse leaves absent fields unset", "[multiace]") {
    const StatusDelta d = parse_ace_status(json{{"swap_phase", "unload"}});
    CHECK(d.swap_phase == std::optional<std::string>{"unload"});
    CHECK_FALSE(d.api_version);
    CHECK_FALSE(d.mode);
    CHECK_FALSE(d.head_ace);
    CHECK_FALSE(d.head_source);
    CHECK_FALSE(d.aces);
    CHECK_FALSE(d.swap_in_progress);
    CHECK_FALSE(d.last_swap_result);
    CHECK_FALSE(d.event_seq);
    CHECK_FALSE(d.spool_binding);

    SECTION("a non-object reads as an empty frame") {
        const StatusDelta none = parse_ace_status(json::array({1, 2}));
        CHECK_FALSE(none.swap_phase);
        CHECK_FALSE(none.aces);
    }
}

TEST_CASE("parse reads the whole head-mode fixture", "[multiace]") {
    const StatusDelta d = parse_ace_status(load_fixture("head_mode_4ace.json"));

    CHECK(d.api_version == std::optional<int>{1});
    CHECK(d.mode == std::optional<Mode>{Mode::HEAD});
    REQUIRE(d.aces);
    REQUIRE(d.aces->size() == 4);
    CHECK(d.event_seq == std::optional<int64_t>{0});
    REQUIRE(d.last_swap_result);
    CHECK(d.last_swap_result->head == -1);
    REQUIRE(d.ace_heads);
    CHECK(*d.ace_heads == std::vector<int>{0, 1, 2, 3});
    CHECK(d.quad_replenish == std::optional<bool>{false});
    REQUIRE(d.spool_binding);
    CHECK(d.spool_binding->empty());

    const Unit& u0 = (*d.aces)[0];
    CHECK(u0.idx == 0);
    CHECK(u0.connected);
    CHECK(u0.model == "ACE Pro");
    CHECK(u0.humidity == std::optional<float>{31.5f});
    CHECK_FALSE((*d.aces)[1].humidity);
    REQUIRE(u0.bays.size() == 4);
    CHECK(u0.bays[0].material == "PLA");
    CHECK(u0.bays[0].brand == "Anycubic");
    CHECK(u0.bays[0].sku == "AHPLRD-101");
    CHECK(u0.bays[0].rfid == 2);
    CHECK(u0.bays[0].color == std::optional<uint32_t>{0xFF0000});
    CHECK(u0.bays[0].status == SlotStatus::AVAILABLE);
    CHECK(u0.bays[2].status == SlotStatus::EMPTY);
}

TEST_CASE("aces arrives whole", "[multiace]") {
    const StatusDelta first = parse_ace_status(load_fixture("head_mode_4ace.json"));
    json shrunk = {{"aces", json::array({load_fixture("head_mode_4ace.json")["aces"][1]})}};
    const StatusDelta second = parse_ace_status(shrunk);
    REQUIRE(first.aces->size() == 4);
    REQUIRE(second.aces);
    REQUIRE(second.aces->size() == 1);
    CHECK((*second.aces)[0].idx == 1);
}

TEST_CASE("head_source null means unloaded", "[multiace]") {
    const StatusDelta d = parse_ace_status(load_fixture("head_mode_4ace.json"));
    REQUIRE(d.head_source);
    const auto& src = *d.head_source;

    REQUIRE(src[0]);
    CHECK(src[0]->ace == 0);
    CHECK(src[0]->slot == 0);
    CHECK(src[0]->type == "PLA");
    CHECK(src[0]->brand == "Anycubic");
    CHECK_FALSE(src[0]->load_failed);
    // "1": null and "3" absent both read as unloaded.
    CHECK_FALSE(src[1]);
    REQUIRE(src[2]);
    CHECK(src[2]->ace == 2);
    CHECK(src[2]->slot == 3);
    CHECK_FALSE(src[3]);

    SECTION("a frame without head_source says nothing about it") {
        CHECK_FALSE(parse_ace_status(json{{"mode", "head"}}).head_source);
    }
    SECTION("an empty dict unloads every head") {
        const StatusDelta empty = parse_ace_status(json{{"head_source", json::object()}});
        REQUIRE(empty.head_source);
        for (const auto& h : *empty.head_source) {
            CHECK_FALSE(h);
        }
    }
    SECTION("load_failed is carried") {
        const StatusDelta failed = parse_ace_status(
            json{{"head_source", {{"1", {{"ace_index", 1}, {"slot", 2}, {"load_failed", true}}}}}});
        REQUIRE((*failed.head_source)[1]);
        CHECK((*failed.head_source)[1]->load_failed);
    }
}

TEST_CASE("colour hex empty is unknown, 000000 is black", "[multiace]") {
    HeadSource src;
    src.color_hex = "";
    CHECK_FALSE(head_source_color(src));
    src.color_hex = "000000";
    CHECK(head_source_color(src) == std::optional<uint32_t>{0x000000});
    src.color_hex = "00FF00";
    CHECK(head_source_color(src) == std::optional<uint32_t>{0x00FF00});

    SECTION("a bay's raw [0,0,0] is black only beside a material") {
        const StatusDelta d = parse_ace_status(load_fixture("head_mode_4ace.json"));
        const Unit& u = (*d.aces)[0];
        CHECK_FALSE(u.bays[2].color); // empty bay, no material
        CHECK(u.bays[3].color == std::optional<uint32_t>{0x000000});
    }
}

TEST_CASE("bay status falls back to the gate flag", "[multiace]") {
    const json ace = {{"aces",
                       {{{"idx", 0},
                         {"gate_status", {1, 0, -1, 1}},
                         {"slots",
                          {{{"index", 0}, {"status", "loading_weird"}},
                           {{"index", 1}, {"status", ""}},
                           {{"index", 2}, {"status", "???"}},
                           {{"index", 3}, {"status", "runout"}}}}}}}};
    const StatusDelta d = parse_ace_status(ace);
    const auto& bays = (*d.aces)[0].bays;
    REQUIRE(bays.size() == 4);
    CHECK(bays[0].status == SlotStatus::AVAILABLE);
    CHECK(bays[1].status == SlotStatus::EMPTY);
    CHECK(bays[2].status == SlotStatus::UNKNOWN);
    CHECK(bays[3].status == SlotStatus::EMPTY);
}

TEST_CASE("dryer state parses through the shared ace helper", "[multiace]") {
    const StatusDelta d = parse_ace_status(load_fixture("head_mode_4ace.json"));
    const DryerInfo& idle = (*d.aces)[0].dryer;
    CHECK(idle.supported);
    CHECK_FALSE(idle.active);
    CHECK(idle.min_temp_c == 35.0f);
    CHECK(idle.max_temp_c == 55.0f);
    const DryerInfo& drying = (*d.aces)[3].dryer;
    CHECK(drying.active);
    CHECK(drying.target_temp_c == 45.0f);
    CHECK(drying.duration_min == 240);
    CHECK(drying.remaining_min == 180);
    CHECK(drying.current_temp_c == 25.0f);
}

TEST_CASE("head_feed matches head_uses_ace", "[multiace]") {
    CHECK(head_feed(Mode::HEAD, false, false) == HeadFeed::ACE);
    CHECK(head_feed(Mode::HEAD, false, true) == HeadFeed::FEEDER);
    CHECK(head_feed(Mode::HEAD, true, true) == HeadFeed::MANUAL);
    CHECK(head_feed(Mode::MULTI, true, false) == HeadFeed::MANUAL);
    // The feeder flag counts only in head mode.
    CHECK(head_feed(Mode::MULTI, false, true) == HeadFeed::ACE);
}

TEST_CASE("candidate bays per mode", "[multiace]") {
    const std::array<int, 4> wiring{2, 0, 1, 3};

    SECTION("head mode: the four bays of the wired unit") {
        const auto bays = candidate_bays(Mode::HEAD, 0, wiring, kAllAce, 4);
        CHECK(bays == std::vector<std::pair<int, int>>{{2, 0}, {2, 1}, {2, 2}, {2, 3}});
        // Wired to a unit that is not connected: nothing.
        CHECK(candidate_bays(Mode::HEAD, 3, wiring, kAllAce, 2).empty());
    }
    SECTION("multi mode: bay h of every unit") {
        const auto bays = candidate_bays(Mode::MULTI, 1, wiring, kAllAce, 3);
        CHECK(bays == std::vector<std::pair<int, int>>{{0, 1}, {1, 1}, {2, 1}});
    }
    SECTION("a feeder or manual head: none") {
        std::array<HeadFeed, 4> feed = kAllAce;
        feed[1] = HeadFeed::FEEDER;
        feed[2] = HeadFeed::MANUAL;
        CHECK(candidate_bays(Mode::HEAD, 1, wiring, feed, 4).empty());
        CHECK(candidate_bays(Mode::MULTI, 2, wiring, feed, 4).empty());
    }
    SECTION("normal mode: none") {
        CHECK(candidate_bays(Mode::NORMAL, 0, wiring, kAllAce, 4).empty());
    }
}

TEST_CASE("head_for_bay refuses a foreign unit in head mode", "[multiace]") {
    const std::array<int, 4> wiring{2, 0, 1, 3};
    CHECK(head_for_bay(Mode::HEAD, 2, 3, wiring, kAllAce) == std::optional<int>{0});
    CHECK(head_for_bay(Mode::HEAD, 0, 0, wiring, kAllAce) == std::optional<int>{1});

    std::array<HeadFeed, 4> feed = kAllAce;
    feed[3] = HeadFeed::FEEDER;
    // Unit 3 is wired only to a feeder head: no ACE head takes its bays.
    CHECK_FALSE(head_for_bay(Mode::HEAD, 3, 1, wiring, feed));

    // Multi mode: bay s feeds head s on every unit, unless that head is not ACE-fed.
    CHECK(head_for_bay(Mode::MULTI, 1, 2, wiring, kAllAce) == std::optional<int>{2});
    CHECK_FALSE(head_for_bay(Mode::MULTI, 1, 3, wiring, feed));
    CHECK_FALSE(head_for_bay(Mode::NORMAL, 0, 0, wiring, kAllAce));
    CHECK_FALSE(head_for_bay(Mode::HEAD, 0, 4, wiring, kAllAce));
}

TEST_CASE("bay global indices follow the heads", "[multiace]") {
    STATIC_REQUIRE(bay_global(0, 0) == 4);
    STATIC_REQUIRE(bay_global(1, 3) == 11);
    STATIC_REQUIRE(bay_global(3, 3) == 19);
    CHECK_FALSE(bay_of(3));
    CHECK(bay_of(4) == std::optional<std::pair<int, int>>{{0, 0}});
    CHECK(bay_of(11) == std::optional<std::pair<int, int>>{{1, 3}});
    for (int k = 0; k < 4; ++k) {
        for (int s = 0; s < 4; ++s) {
            CHECK(bay_of(bay_global(k, s)) == std::optional<std::pair<int, int>>{{k, s}});
        }
    }
}

TEST_CASE("event line parse", "[multiace]") {
    std::ifstream f(fixture_path("events.txt"));
    REQUIRE(f.is_open());
    std::vector<Event> events;
    for (std::string line; std::getline(f, line);) {
        if (auto ev = parse_event_line(line)) {
            events.push_back(*ev);
        }
    }
    REQUIRE(events.size() == 5);
    CHECK(events[0].name == "swap_imminent");
    CHECK(events[0].fields.at("from_slot") == "0");
    CHECK(events[0].seq == 1);
    CHECK(events[2].name == "swap_done");
    CHECK(events[2].seq == 3);
    CHECK(events[3].name == "swap_failed");
    CHECK(events[3].fields.at("status") == "load_failed");
    CHECK(events[3].fields.at("head") == "2");
    CHECK(events[4].fields.at("delta") == "4.2");

    SECTION("malformed lines are rejected") {
        CHECK_FALSE(parse_event_line("// ACE_LOAD_HEAD HEAD=1"));
        CHECK_FALSE(parse_event_line("multiace_event"));
        CHECK_FALSE(parse_event_line("multiace_event head=1 seq=2"));
        CHECK_FALSE(parse_event_line("multiace_event swap_done head seq=2"));
        CHECK_FALSE(parse_event_line("multiace_event swap_done =1 seq=2"));
        CHECK_FALSE(parse_event_line("multiace_event swap_done head=1 seq=x"));
        CHECK_FALSE(parse_event_line("!! multiace_event swap_done seq=2"));
    }
    SECTION("seq is optional") {
        auto ev = parse_event_line("multiace_event airlog_chew head=0 delta=9 run=3");
        REQUIRE(ev);
        CHECK(ev->seq == -1);
    }
}

TEST_CASE("multiace gcode spellings", "[multiace]") {
    CHECK(load_gcode(1, std::nullopt) == "ACE_LOAD_HEAD HEAD=1");
    CHECK(load_gcode(2, std::make_pair(1, 3)) == "ACE_LOAD_HEAD HEAD=2 ACE=1 SLOT=3");
    CHECK(unload_gcode(3) == "ACE_UNLOAD_HEAD HEAD=3");
    CHECK(unload_all_gcode() == "ACE_UNLOAD_ALL_HEADS");
    CHECK(dry_gcode(2, 45, 240) == "ACE_DRY ACE=2 TEMP=45 DURATION=240");
    CHECK(stop_dry_gcode(2) == "ACE_STOP_DRYING ACE=2");
}

TEST_CASE("the other fixtures parse to their modes", "[multiace]") {
    const StatusDelta multi = parse_ace_status(load_fixture("multi_mode_2ace.json"));
    CHECK(multi.mode == std::optional<Mode>{Mode::MULTI});
    CHECK(multi.aces->size() == 2);
    CHECK((*multi.head_manual)[3]);

    const StatusDelta feeders = parse_ace_status(load_fixture("head_mode_2ace_2feeder.json"));
    CHECK(*feeders.head_feeder == std::array<bool, 4>{false, false, true, true});
    CHECK(*feeders.head_ace == std::array<int, 4>{0, 1, 0, 0});

    const StatusDelta normal = parse_ace_status(load_fixture("normal_mode.json"));
    CHECK(normal.mode == std::optional<Mode>{Mode::NORMAL});
    CHECK(normal.aces->empty());

    const json frames = load_fixture("delta_swap_sequence.json");
    REQUIRE(frames.is_array());
    const StatusDelta done = parse_ace_status(frames[3]["ace"]);
    CHECK(done.swap_in_progress == std::optional<bool>{false});
    REQUIRE(done.last_swap_result);
    CHECK(done.last_swap_result->head == 1);
    CHECK(done.last_swap_result->status == "ok");
    CHECK((*done.head_source)[1]->slot == 2);
}
