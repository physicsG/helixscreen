// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_temperature_service_chamber.cpp
 * @brief The filament mini graph charts the chamber sensor PrinterState resolved.
 *
 * The rule itself is pinned in test_chamber_heater_assignment.cpp.
 */

#include "../lvgl_test_fixture.h"
#include "../test_helpers/printer_state_test_access.h"
#include "../test_helpers/temperature_service_test_access.h"
#include "app_globals.h"
#include "printer_discovery.h"
#include "printer_state.h"
#include "settings_manager.h"
#include "temp_graph_controller.h"
#include "temperature_sensor_manager.h"
#include "temperature_service.h"

#include <initializer_list>
#include <memory>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

namespace {

/// A TemperatureService over the PrinterState singleton, the state the graph
/// controller reads its subjects from.
struct MiniGraphHarness {
    helix::PrinterState& state = get_printer_state();
    std::unique_ptr<TemperatureService> service;

    MiniGraphHarness() {
        helix::PrinterStateTestAccess::reset(state);
        state.init_subjects(false);
        helix::SettingsManager::instance().init_subjects();
        helix::sensors::TemperatureSensorManager::instance().init_subjects();
        service = std::make_unique<TemperatureService>(state, nullptr);
    }

    ~MiniGraphHarness() {
        service.reset();
        helix::SettingsManager::instance().set_chamber_sensor_assignment("auto");
    }

    /// Discovery lands with these objects under this chamber sensor assignment.
    void discover(const char* sensor_assignment, std::initializer_list<const char*> objects) {
        helix::SettingsManager::instance().set_chamber_sensor_assignment(sensor_assignment);
        helix::PrinterDiscovery hw;
        nlohmann::json list = nlohmann::json::array();
        for (const char* object : objects) {
            list.push_back(object);
        }
        hw.parse_objects(list);
        state.set_hardware(std::move(hw));
    }

    /// The filament panel builds its mini graph in @p container.
    const helix::TempGraphController& build(lv_obj_t* container) {
        service->setup_mini_combined_graph(container);
        const auto* graph = TemperatureServiceTestAccess::mini_graph(*service);
        REQUIRE(graph != nullptr);
        REQUIRE(graph->is_valid());
        REQUIRE(graph->series_id_for("heater_bed") != SeriesId::None);
        return *graph;
    }
};

} // namespace

TEST_CASE_METHOD(LVGLTestFixture,
                 "The filament mini graph charts the chamber sensor PrinterState resolved",
                 "[temperature_service][chamber]") {
    MiniGraphHarness h;
    lv_obj_t* container = lv_obj_create(test_screen());

    SECTION("an assigned sensor no chamber keyword names") {
        h.discover("temperature_sensor external_bme",
                   {"temperature_sensor external_bme", "extruder", "heater_bed"});
        REQUIRE_FALSE(h.state.get_discovery().has_chamber_sensor());

        const auto& graph = h.build(container);
        CHECK(graph.series_id_for("temperature_sensor external_bme") != SeriesId::None);
    }
    SECTION("never a sensor the printer is set not to use") {
        h.discover("none", {"temperature_sensor chamber", "extruder", "heater_bed"});
        REQUIRE(h.state.get_discovery().has_chamber_sensor());

        const auto& graph = h.build(container);
        CHECK(graph.series_id_for("temperature_sensor chamber") == SeriesId::None);
    }
    SECTION("a saved sensor the printer does not report charts the one it does") {
        h.discover("temperature_sensor box",
                   {"temperature_sensor chamber", "temperature_sensor mcu_toolhead", "extruder",
                    "heater_bed"});

        const auto& graph = h.build(container);
        CHECK(graph.series_id_for("temperature_sensor chamber") != SeriesId::None);
        CHECK(graph.series_id_for("temperature_sensor box") == SeriesId::None);
    }
}
