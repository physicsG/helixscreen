// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_wizard_helpers.h"
#include "ui_wizard_led_select.h"

#include "../lvgl_ui_test_fixture.h"
#include "app_globals.h"
#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"

#include <string>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

TEST_CASE("wizard dropdown: a device-type namer labels each row, None stays first",
          "[wizard][dropdown]") {
    using helix::ui::wizard::build_dropdown_options;
    using helix::ui::wizard::display_name_for;
    CHECK(build_dropdown_options({"heater_fan hotend_fan", "fan"}, nullptr, true,
                                 display_name_for(helix::DeviceType::FAN)) ==
          "None\n" + helix::get_display_name("heater_fan hotend_fan", helix::DeviceType::FAN) +
              "\n" + helix::get_display_name("fan", helix::DeviceType::FAN));
    CHECK(build_dropdown_options({"heater_fan hotend_fan"}, nullptr, false) ==
          "heater_fan hotend_fan");
}

TEST_CASE_METHOD(LVGLUITestFixture,
                 "wizard LED step names each light the way the LEDs overlay does",
                 "[wizard][led][dropdown]") {
    MoonrakerClientMock client{MoonrakerClientMock::PrinterType::VORON_24};
    helix::PrinterState state;
    MoonrakerAPIMock api{client, state};
    api.hardware().parse_objects(
        nlohmann::json::array({"led caselight", "neopixel chamber_light", "extruder"}));
    set_moonraker_api(&api);
    struct ApiGuard {
        ~ApiGuard() {
            set_moonraker_api(nullptr);
        }
    } api_guard;

    auto* step = get_wizard_led_select_step();
    step->init_subjects();
    lv_obj_t* root = step->create(test_screen());
    REQUIRE(root != nullptr);
    lv_obj_t* dropdown = lv_obj_find_by_name(root, "led_main_dropdown");
    REQUIRE(dropdown != nullptr);

    // get_display_name(..., DeviceType::LED) would read "Caselight LED".
    CHECK(std::string(lv_dropdown_get_options(dropdown)) == "None\nCaselight\nChamber Light");
}
