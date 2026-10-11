// tests/unit/test_ams_backend_snapmaker_actions.cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../helix_test_fixture.h"
#include "../test_helpers/snapmaker_test_access.h"
#include "ams_backend_snapmaker.h"

#include <algorithm>
#include <any>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using namespace helix;
using namespace helix::printer;

namespace {
/// An unwrapped status object carrying only print_task_config — the shape the
/// initial query response sends and handle_status() takes.
nlohmann::json frame(const nlohmann::json& ptc) {
    nlohmann::json p = nlohmann::json::object();
    p["print_task_config"] = ptc;
    return p;
}
const DeviceAction* find_action(const std::vector<DeviceAction>& as, const std::string& id) {
    auto it = std::find_if(as.begin(), as.end(), [&](const DeviceAction& a) { return a.id == id; });
    return it == as.end() ? nullptr : &(*it);
}
} // namespace

TEST_CASE("snapmaker exposes its firmware settings as device actions",
          "[ams][snapmaker][actions]") {
    HelixTestFixture fixture;
    AmsBackendSnapmaker backend(nullptr, nullptr);
    SnapmakerTestAccess::handle_status(backend, frame({{"auto_replenish_filament", true},
                                                       {"replenish_ignore_color", false},
                                                       {"filament_entangle_detect", false},
                                                       {"filament_entangle_sen", "medium"},
                                                       {"end_led_turn_off", true}}));
    const auto actions = backend.get_device_actions();

    const DeviceAction* rep = find_action(actions, "snapmaker_auto_replenish");
    REQUIRE(rep != nullptr);
    REQUIRE(rep->type == ActionType::TOGGLE);
    REQUIRE(std::any_cast<bool>(rep->current_value) == true);

    const DeviceAction* color = find_action(actions, "snapmaker_replenish_ignore_color");
    REQUIRE(color != nullptr);
    REQUIRE(color->type == ActionType::TOGGLE);
    REQUIRE(std::any_cast<bool>(color->current_value) == false);

    const DeviceAction* tangle = find_action(actions, "snapmaker_entangle_detect");
    REQUIRE(tangle != nullptr);
    REQUIRE(tangle->type == ActionType::TOGGLE);
    REQUIRE(std::any_cast<bool>(tangle->current_value) == false);

    const DeviceAction* sen = find_action(actions, "snapmaker_entangle_sen");
    REQUIRE(sen != nullptr);
    REQUIRE(sen->type == ActionType::DROPDOWN);
    REQUIRE(sen->options == std::vector<std::string>{"low", "medium", "high"});
    REQUIRE(std::any_cast<std::string>(sen->current_value) == "medium");
}

TEST_CASE("a setting the firmware has not reported is not offered", "[ams][snapmaker][actions]") {
    // Offering a toggle whose state we do not know would render it off and
    // invite the user to "change" it to the value it already has.
    HelixTestFixture fixture;
    AmsBackendSnapmaker backend(nullptr, nullptr);
    const auto actions = backend.get_device_actions();
    REQUIRE(find_action(actions, "snapmaker_auto_replenish") == nullptr);
}

TEST_CASE("end-unload offers one toggle per reported toolhead", "[ams][snapmaker][actions]") {
    HelixTestFixture fixture;
    AmsBackendSnapmaker backend(nullptr, nullptr);
    SnapmakerTestAccess::handle_status(
        backend, frame({{"end_unload_filament", {true, false, false, true}}}));
    const auto actions = backend.get_device_actions();
    REQUIRE(find_action(actions, "snapmaker_end_unload_t0") != nullptr);
    REQUIRE(find_action(actions, "snapmaker_end_unload_t3") != nullptr);
    REQUIRE(find_action(actions, "snapmaker_end_unload_t4") == nullptr);
    REQUIRE(std::any_cast<bool>(find_action(actions, "snapmaker_end_unload_t0")->current_value) ==
            true);
    REQUIRE(std::any_cast<bool>(find_action(actions, "snapmaker_end_unload_t1")->current_value) ==
            false);
}

TEST_CASE("executing a toggle emits only that field", "[ams][snapmaker][actions]") {
    HelixTestFixture fixture;
    AmsBackendSnapmaker backend(nullptr, nullptr);
    SnapmakerTestAccess::handle_status(backend, frame({{"end_led_turn_off", false}}));
    const std::string g = backend.build_preference_gcode("snapmaker_end_led_off", std::any(true));
    REQUIRE(g == "SET_PRINT_PREFERENCES END_LED_TURN_OFF=1");
}

TEST_CASE("executing the per-tool toggle rewrites the whole array", "[ams][snapmaker][actions]") {
    // END_UNLOAD_FILAMENT takes the full list, so flipping one tool must send
    // the others at their current values or they are cleared.
    HelixTestFixture fixture;
    AmsBackendSnapmaker backend(nullptr, nullptr);
    SnapmakerTestAccess::handle_status(
        backend, frame({{"end_unload_filament", {true, false, true, false}}}));
    const std::string g = backend.build_preference_gcode("snapmaker_end_unload_t1", std::any(true));
    REQUIRE(g == "SET_PRINT_PREFERENCES END_UNLOAD_FILAMENT=[1,1,1,0]");
}

TEST_CASE("an unknown action id produces no gcode", "[ams][snapmaker][actions]") {
    HelixTestFixture fixture;
    AmsBackendSnapmaker backend(nullptr, nullptr);
    REQUIRE(backend.build_preference_gcode("not_a_real_action", std::any(true)).empty());
}

TEST_CASE("the replenish and tangle toggles each emit only their own field",
          "[ams][snapmaker][actions]") {
    HelixTestFixture fixture;
    AmsBackendSnapmaker backend(nullptr, nullptr);
    SnapmakerTestAccess::handle_status(
        backend, frame({{"replenish_ignore_color", false}, {"filament_entangle_detect", true}}));
    REQUIRE(backend.build_preference_gcode("snapmaker_replenish_ignore_color", std::any(true)) ==
            "SET_PRINT_PREFERENCES REPLENISH_IGNORE_COLOR=1");
    REQUIRE(backend.build_preference_gcode("snapmaker_entangle_detect", std::any(false)) ==
            "SET_PRINT_PREFERENCES FILAMENT_ENTANGLE_DETECT=0");
}

TEST_CASE("an overlong tool suffix is refused rather than parsed", "[ams][snapmaker][actions]") {
    // stoul throws out_of_range past ULONG_MAX, and this mapping runs inside
    // the UI callback that invoked the action. No real tool index is that
    // large, so an all-digit suffix this long can only be a malformed id.
    HelixTestFixture fixture;
    AmsBackendSnapmaker backend(nullptr, nullptr);
    REQUIRE(
        backend.build_preference_gcode("snapmaker_end_unload_t99999999999999999999", std::any(true))
            .empty());
}

TEST_CASE("a value whose type does not match the action is refused, not thrown",
          "[ams][snapmaker][actions]") {
    // The overlay types each action's value by the control it renders, so a
    // mismatched type is a malformed payload and must read as "not ours" —
    // the same refusal a malformed id gets — because a bad_any_cast out of
    // here would escape the UI-thread action callback uncaught.
    HelixTestFixture fixture;
    AmsBackendSnapmaker backend(nullptr, nullptr);
    SnapmakerTestAccess::handle_status(backend, frame({{"end_unload_filament", {false, false}}}));
    REQUIRE(backend.build_preference_gcode("snapmaker_auto_replenish", std::any(std::string("on")))
                .empty());
    REQUIRE(
        backend
            .build_preference_gcode("snapmaker_replenish_ignore_color", std::any(std::string("on")))
            .empty());
    REQUIRE(backend.build_preference_gcode("snapmaker_entangle_detect", std::any(std::string("on")))
                .empty());
    REQUIRE(backend.build_preference_gcode("snapmaker_end_led_off", std::any(std::string("on")))
                .empty());
    REQUIRE(backend.build_preference_gcode("snapmaker_end_unload_t0", std::any(std::string("on")))
                .empty());
    REQUIRE(backend.build_preference_gcode("snapmaker_entangle_sen", std::any(true)).empty());

    // The public entry the UI callback drives must refuse the same way rather
    // than let the exception cross the virtual.
    const AmsError err = backend.execute_device_action("snapmaker_end_led_off", std::any(1));
    REQUIRE(err.result == AmsResult::NOT_SUPPORTED);
}
