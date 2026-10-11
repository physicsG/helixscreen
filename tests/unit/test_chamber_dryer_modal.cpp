// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// The chamber dryer's start modal (#1299): presets labelled as the dryer will
// run them, and Start sending the cycle plus the bed assist through
// TemperatureController.

#include "ui_chamber_dryer_modal.h"
#include "ui_modal.h"

#include "../lvgl_ui_test_fixture.h"
#include "app_globals.h"
#include "chamber_heater_backend.h"
#include "moonraker_api.h"
#include "moonraker_client_mock.h"
#include "panel_widget_manager.h"
#include "printer_state.h"
#include "temperature_controller.h"

#include <algorithm>
#include <memory>
#include <string>

#include "../catch_amalgamated.hpp"

using helix::ui::ChamberDryerModal;

TEST_CASE("chamber dryer preset labels show what the dryer will run",
          "[chamber][dryer][modal][1299]") {
    const helix::DryerInfo stock =
        helix::chamber::backend_by_id("panda_breath")->dryer_capabilities();
    CHECK(ChamberDryerModal::preset_label({"PLA", 55.0f, 240}, stock) == "PLA 55°C/4h");
    // Above the cycle's ceiling, and a length the firmware only takes in hours.
    CHECK(ChamberDryerModal::preset_label({"PETG", 65.0f, 90}, stock) == "PETG 60°C/2h");
}

TEST_CASE_METHOD(LVGLUITestFixture, "chamber dryer modal starts the chosen preset",
                 "[chamber][dryer][modal][1299]") {
    MoonrakerClientMock client(MoonrakerClientMock::PrinterType::VORON_24);
    MoonrakerAPI api(client, state());
    helix::TemperatureController controller(state(), &api);
    state().set_klippy_state_sync(helix::KlippyState::READY);
    helix::PanelWidgetManager::instance().register_shared_resource<helix::TemperatureController>(
        &controller);

    SECTION("no dryer, no modal") {
        controller.set_chamber_dryer(helix::chamber::backend_by_id("dragonbreath"), true);
        CHECK_FALSE(ChamberDryerModal::show_owned());
    }

    SECTION("Start sends the first preset and heats the bed") {
        controller.set_chamber_dryer(helix::chamber::backend_by_id("panda_breath"), true);
        REQUIRE(ChamberDryerModal::show_owned());
        lv_obj_t* dialog = ModalStack::instance().top_dialog();
        REQUIRE(dialog != nullptr);
        CHECK(ModalStack::instance().top_component_name() == "chamber_dryer_modal");

        const auto presets = helix::get_default_drying_presets();
        REQUIRE_FALSE(presets.empty());
        lv_obj_t* dropdown = lv_obj_find_by_name(dialog, "preset_dropdown");
        REQUIRE(dropdown != nullptr);
        CHECK(lv_dropdown_get_option_count(dropdown) == presets.size());
        char first[64] = {};
        lv_dropdown_set_selected(dropdown, 0);
        lv_dropdown_get_selected_str(dropdown, first, sizeof(first));
        const helix::DryerInfo dryer = controller.chamber_dryer();
        CHECK(std::string(first) == ChamberDryerModal::preset_label(presets[0], dryer));

        lv_obj_t* bed_label = lv_obj_find_by_name(dialog, "bed_assist_label");
        REQUIRE(bed_label != nullptr);
        CHECK(std::string(lv_label_get_text(bed_label)).find("70") != std::string::npos);

        client.clear_gcode_script_history();
        lv_obj_t* start = lv_obj_find_by_name(dialog, "btn_primary");
        REQUIRE(start != nullptr);
        lv_obj_send_event(start, LV_EVENT_CLICKED, nullptr);
        process_lvgl(50);

        const auto& history = client.gcode_script_history();
        const std::string expected_start =
            helix::chamber::backend_by_id("panda_breath")
                ->dryer_start_gcode(dryer.clamp_temp(presets[0].temp_c),
                                    dryer.clamp_duration(presets[0].duration_min));
        CHECK(std::count(history.begin(), history.end(), expected_start) == 1);
        CHECK(std::count(history.begin(), history.end(),
                         "SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=70") == 1);
    }

    SECTION("bed assist switched off heats nothing but the dryer") {
        controller.set_chamber_dryer(helix::chamber::backend_by_id("panda_breath"), true);
        REQUIRE(ChamberDryerModal::show_owned());
        lv_obj_t* dialog = ModalStack::instance().top_dialog();
        REQUIRE(dialog != nullptr);
        lv_obj_t* bed_switch = lv_obj_find_by_name(dialog, "bed_assist_switch");
        REQUIRE(bed_switch != nullptr);
        CHECK(lv_obj_has_state(bed_switch, LV_STATE_CHECKED));
        lv_obj_remove_state(bed_switch, LV_STATE_CHECKED);

        client.clear_gcode_script_history();
        lv_obj_send_event(lv_obj_find_by_name(dialog, "btn_primary"), LV_EVENT_CLICKED, nullptr);
        process_lvgl(50);
        const auto& history = client.gcode_script_history();
        CHECK(std::count_if(history.begin(), history.end(), [](const std::string& line) {
                  return line.rfind("PANDA_BREATH_DRY_START", 0) == 0;
              }) == 1);
        CHECK(std::none_of(history.begin(), history.end(), [](const std::string& line) {
            return line.find("heater_bed") != std::string::npos;
        }));
    }

    ModalStack::instance().clear();
    process_lvgl(50);
    helix::PanelWidgetManager::instance().register_shared_resource<helix::TemperatureController>(
        std::shared_ptr<helix::TemperatureController>{});
}
