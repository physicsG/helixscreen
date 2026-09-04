// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_chamber_ceiling_actions.cpp
 * @brief Chamber-heater backend ceiling fallback + fault-reset/filter-fan
 *        actions on TemperatureController (issue #1290).
 *
 * Ceiling: the configfile max_temp always wins; when the query is silent,
 * the matched backend's conservative cap applies (60 C for appliances), and
 * a generic backend (cap 0) keeps the heater default.
 *
 * Actions: reset_chamber_fault() / set_chamber_filter_fan() send
 * backend-provided gcode through the standard api; empty wiring is a no-op.
 * PrinterState::set_hardware wires both from the discovery-matched backend,
 * and a manual chamber-heater override detaches them again.
 *
 * The stock mock answers printer.objects.query configfile from a hard-coded
 * config, so ConfigfileMockClient below intercepts exactly that method and
 * answers from test-controlled sections; everything else (gcode recording,
 * discovery) delegates to the stock mock.
 */

#include "../lvgl_test_fixture.h"
#include "app_globals.h"
#include "moonraker_api.h"
#include "moonraker_client_mock.h"
#include "panel_widget_manager.h"
#include "printer_discovery.h"
#include "printer_state.h"
#include "settings_manager.h"
#include "temperature_controller.h"
#include "test_helpers/update_queue_test_access.h"

#include <functional>
#include <optional>
#include <string>

#include "../catch_amalgamated.hpp"

namespace {

/// MoonrakerClientMock whose configfile.config answer is test-controlled.
class ConfigfileMockClient : public MoonrakerClientMock {
  public:
    using MoonrakerClientMock::MoonrakerClientMock;

    /// Sections returned for a configfile query. Empty object = silent
    /// configfile (no max_temp anywhere).
    nlohmann::json config_sections = nlohmann::json::object();

    helix::RequestId send_jsonrpc(
        const std::string& method, const nlohmann::json& params,
        std::function<void(const nlohmann::json&)> success_cb,
        std::function<void(const MoonrakerError&)> error_cb, uint32_t timeout_ms = 0,
        bool silent = false,
        std::optional<helix::rpc_error_policy::CallerIntent> intent = std::nullopt) override {
        if (method == "printer.objects.query" && params.contains("objects") &&
            params["objects"].contains("configfile")) {
            if (success_cb) {
                success_cb(
                    {{"result", {{"status", {{"configfile", {{"config", config_sections}}}}}}}});
            }
            return 0;
        }
        return MoonrakerClientMock::send_jsonrpc(method, params, std::move(success_cb),
                                                 std::move(error_cb), timeout_ms, silent, intent);
    }
};

struct ChamberFixture : public LVGLTestFixture {
    ConfigfileMockClient client;
    helix::PrinterState state;
    MoonrakerAPI api;
    helix::TemperatureController controller;

    ChamberFixture()
        : client(MoonrakerClientMock::PrinterType::VORON_24), api(client, state),
          controller(state, &api) {
        state.init_subjects(false);
        helix::SettingsManager::instance().set_chamber_heater_assignment("auto");
        // execute_gcode gates on klippy state; the subject defaults to SHUTDOWN.
        state.set_klippy_state_sync(helix::KlippyState::READY);
    }

    ~ChamberFixture() override {
        helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
    }

    /// Resolve a dragonbreath chamber heater through the production path
    /// (discovery parse_objects → set_hardware chamber resolution).
    void discover_dragonbreath() {
        helix::PrinterDiscovery hardware;
        nlohmann::json objects = {"heater_generic dragonbreath", "extruder", "heater_bed"};
        hardware.parse_objects(objects);
        REQUIRE(hardware.chamber_heater_name() == "heater_generic dragonbreath");
        REQUIRE(hardware.chamber_heater_backend_id() == "dragonbreath");
        state.set_hardware(hardware);
        REQUIRE(controller.resolved_name(helix::HeaterType::Chamber) ==
                "heater_generic dragonbreath");
    }
};

} // namespace

TEST_CASE("conservative ceiling applies when configfile silent", "[chamber][ceiling]") {
    ChamberFixture f;
    f.discover_dragonbreath();

    SECTION("backend conservative cap becomes the configured max") {
        // DragonBreath wiring: 60 = the backend's conservative chamber cap.
        f.controller.set_chamber_actions("", "", 60.0);

        f.controller.ensure_limits(helix::HeaterType::Chamber);
        helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());

        REQUIRE(f.controller.configured_max(helix::HeaterType::Chamber) == 60);
    }

    SECTION("generic backend (cap 0) keeps the heater default") {
        f.controller.set_chamber_actions("", "", 0.0);

        f.controller.ensure_limits(helix::HeaterType::Chamber);
        helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());

        // No fallback: the configured max stays unknown and the keypad keeps
        // its 80 C chamber default — current behavior for silent configfile.
        REQUIRE(f.controller.configured_max(helix::HeaterType::Chamber) == 0);
        REQUIRE(f.controller.keypad_range(helix::HeaterType::Chamber).max == 80.0f);
    }
}

TEST_CASE("configfile max_temp beats conservative ceiling", "[chamber][ceiling]") {
    ChamberFixture f;
    f.discover_dragonbreath();

    f.client.config_sections = {{"heater_generic dragonbreath", {{"max_temp", 75}}}};
    f.controller.set_chamber_actions("", "", 60.0);

    f.controller.ensure_limits(helix::HeaterType::Chamber);
    helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());

    REQUIRE(f.controller.configured_max(helix::HeaterType::Chamber) == 75);
}

TEST_CASE("fault reset + filter fan send backend gcode", "[chamber][actions]") {
    ChamberFixture f;

    SECTION("backend actions round-trip as gcode") {
        f.controller.set_chamber_actions("DRAGONBREATH_RESET", "output_pin dragonbreath_filter",
                                         60.0);

        f.client.clear_gcode_script_history();
        f.controller.reset_chamber_fault();
        REQUIRE(f.client.gcode_script_history().size() == 1);
        REQUIRE(f.client.gcode_script_history()[0] == "DRAGONBREATH_RESET");

        // SET_PIN takes the BARE pin name — the "output_pin " prefix is stripped.
        f.client.clear_gcode_script_history();
        f.controller.set_chamber_filter_fan(true);
        REQUIRE(f.client.gcode_script_history().size() == 1);
        REQUIRE(f.client.gcode_script_history()[0] == "SET_PIN PIN=dragonbreath_filter VALUE=1");

        f.client.clear_gcode_script_history();
        f.controller.set_chamber_filter_fan(false);
        REQUIRE(f.client.gcode_script_history().size() == 1);
        REQUIRE(f.client.gcode_script_history()[0] == "SET_PIN PIN=dragonbreath_filter VALUE=0");
    }

    SECTION("empty wiring is a clean no-op") {
        f.controller.set_chamber_actions("", "", 0.0);

        f.client.clear_gcode_script_history();
        f.controller.reset_chamber_fault();
        f.controller.set_chamber_filter_fan(true);
        REQUIRE(f.client.gcode_script_history().empty());
    }
}

TEST_CASE("set_hardware wires backend actions into the controller", "[chamber][actions]") {
    ChamberFixture f;

    // PrinterState hands the actions to the GLOBAL controller (app_globals),
    // so register the fixture's controller for the production wire-up.
    helix::PanelWidgetManager::instance().register_shared_resource<helix::TemperatureController>(
        &f.controller);

    SECTION("dragonbreath discovery arms reset gcode + filter pin") {
        f.discover_dragonbreath();

        f.client.clear_gcode_script_history();
        f.controller.reset_chamber_fault();
        f.controller.set_chamber_filter_fan(true);
        REQUIRE(f.client.gcode_script_history().size() == 2);
        REQUIRE(f.client.gcode_script_history()[0] == "DRAGONBREATH_RESET");
        REQUIRE(f.client.gcode_script_history()[1] == "SET_PIN PIN=dragonbreath_filter VALUE=1");
    }

    SECTION("manual chamber-heater override detaches the backend actions") {
        f.discover_dragonbreath();
        // Override to "none": the resolved heater is no longer the discovery
        // pick, so a fault reset / fan toggle would aim at the wrong heater —
        // the actions must clear along with the diagnostics source (Task 4).
        helix::SettingsManager::instance().set_chamber_heater_assignment("none");
        helix::PrinterDiscovery hardware;
        nlohmann::json objects = {"heater_generic dragonbreath", "extruder", "heater_bed"};
        hardware.parse_objects(objects);
        f.state.set_hardware(hardware);

        f.client.clear_gcode_script_history();
        f.controller.reset_chamber_fault();
        f.controller.set_chamber_filter_fan(true);
        REQUIRE(f.client.gcode_script_history().empty());
    }

    // Drop the registration so later tests' set_hardware sees no controller
    // instead of this fixture's soon-to-be-destroyed one, and restore the
    // global chamber assignment the override section disturbed.
    helix::PanelWidgetManager::instance().register_shared_resource<helix::TemperatureController>(
        std::shared_ptr<helix::TemperatureController>{});
    helix::SettingsManager::instance().set_chamber_heater_assignment("auto");
}
