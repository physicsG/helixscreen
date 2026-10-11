// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afc_defaults.h"
#include "ams_backend_ace.h"
#include "ams_backend_cfs.h"
#include "ams_types.h"
#include "hh_defaults.h"

#include <algorithm>
#include <any>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::printer;

namespace {

const DeviceAction& by_id(const std::vector<DeviceAction>& as, const std::string& id) {
    auto it = std::find_if(as.begin(), as.end(), [&](const DeviceAction& a) { return a.id == id; });
    REQUIRE(it != as.end());
    return *it;
}

} // namespace

TEST_CASE("DeviceAction factories set every field the control type uses", "[ams][device_actions]") {
    SECTION("button") {
        auto a = DeviceAction::button("id", "Label", "sec", "icon", "hint");
        CHECK(a.id == "id");
        CHECK(a.label == "Label");
        CHECK(a.section == "sec");
        CHECK(a.icon == "icon");
        CHECK(a.description == "hint");
        CHECK(a.type == ActionType::BUTTON);
        CHECK_FALSE(a.current_value.has_value());
        CHECK(a.slot_index == -1);
        CHECK(a.enabled);
        CHECK(a.disable_reason.empty());
    }
    SECTION("toggle keeps the value's type") {
        auto a = DeviceAction::toggle("id", "L", "s", true);
        CHECK(a.type == ActionType::TOGGLE);
        CHECK(std::any_cast<bool>(a.current_value) == true);
        CHECK(a.icon.empty());
        CHECK_FALSE(DeviceAction::toggle("id", "L", "s").current_value.has_value());
    }
    SECTION("slider") {
        auto a = DeviceAction::slider("id", "L", "s", 450.0f, 100.0f, 2000.0f, "mm", "ruler", "d");
        CHECK(a.type == ActionType::SLIDER);
        CHECK(std::any_cast<float>(a.current_value) == 450.0f);
        CHECK(a.min_value == 100.0f);
        CHECK(a.max_value == 2000.0f);
        CHECK(a.unit == "mm");
        CHECK(a.icon == "ruler");
        CHECK(a.description == "d");
    }
    SECTION("dropdown") {
        auto a = DeviceAction::dropdown("id", "L", "s", {"a", "b"}, std::string("b"));
        CHECK(a.type == ActionType::DROPDOWN);
        CHECK(a.options == std::vector<std::string>{"a", "b"});
        CHECK(std::any_cast<std::string>(a.current_value) == "b");
    }
}

TEST_CASE("AFC default actions keep their shipped field values", "[ams][afc][device_actions]") {
    const auto actions = afc_default_actions();
    CHECK(actions.size() == 26);

    const auto& bowden = by_id(actions, "bowden_length");
    CHECK(bowden.label == "Bowden Length");
    CHECK(bowden.icon == "ruler");
    CHECK(bowden.section == "setup");
    CHECK(bowden.description == "Distance from hub to toolhead");
    CHECK(bowden.type == ActionType::SLIDER);
    CHECK(std::any_cast<float>(bowden.current_value) == 450.0f);
    CHECK(bowden.min_value == 100.0f);
    CHECK(bowden.max_value == 2000.0f);
    CHECK(bowden.unit == "mm");

    const auto& cut = by_id(actions, "hub_cut_enabled");
    CHECK(cut.type == ActionType::TOGGLE);
    CHECK(std::any_cast<bool>(cut.current_value) == false);
    CHECK(cut.icon == "content-cut");
    CHECK(cut.section == "hub");

    const auto& park = by_id(actions, "park");
    CHECK(park.type == ActionType::BUTTON);
    CHECK(park.icon == "parking");
    CHECK(park.description == "Park the AFC system");
    CHECK_FALSE(park.current_value.has_value());
}

TEST_CASE("Happy Hare default actions keep their shipped field values",
          "[ams][happy_hare][device_actions]") {
    const auto actions = hh_default_actions();
    CHECK(actions.size() == 32);

    const auto& speed = by_id(actions, "gear_from_buffer_speed");
    CHECK(speed.type == ActionType::SLIDER);
    CHECK(std::any_cast<double>(speed.current_value) == 150.0);
    CHECK(speed.min_value == 10.0f);
    CHECK(speed.max_value == 300.0f);
    CHECK(speed.unit == "mm/s");
    CHECK(speed.icon.empty());

    const auto& led = by_id(actions, "led_mode");
    CHECK(led.type == ActionType::DROPDOWN);
    CHECK(led.options == std::vector<std::string>{"off", "gate_status", "filament_color", "on"});
    CHECK(std::any_cast<std::string>(led.current_value) == "off");

    CHECK(std::any_cast<bool>(by_id(actions, "motors_toggle").current_value) == true);
    CHECK(std::any_cast<bool>(by_id(actions, "gear_sync").current_value) == false);
    CHECK(by_id(actions, "servo_buzz").type == ActionType::BUTTON);
}

TEST_CASE("ACE and CFS device actions keep their shipped field values",
          "[ams][ace][cfs][device_actions]") {
    const auto ace = helix::AmsBackendAce(nullptr, nullptr).get_device_actions();
    REQUIRE(ace.size() == 3);
    CHECK(ace[0].id == "ace_manual_feed");
    CHECK(ace[0].section == "filament_control");
    CHECK(ace[0].description == "Feed filament from current slot");
    CHECK(ace[0].type == ActionType::BUTTON);
    CHECK(ace[2].id == "ace_feed_assist_toggle");
    CHECK(ace[2].type == ActionType::TOGGLE);
    CHECK_FALSE(ace[2].current_value.has_value());

    const auto cfs = AmsBackendCfs(nullptr, nullptr).get_device_actions();
    REQUIRE(cfs.size() == 4);
    CHECK(cfs[1].id == "toggle_auto_refill");
    CHECK(cfs[1].type == ActionType::TOGGLE);
    CHECK(cfs[1].section == "maintenance");
    CHECK(cfs[3].label == "Communication Test");
    CHECK(cfs[3].enabled);
}
