// SPDX-License-Identifier: GPL-3.0-or-later

#include "../helix_test_fixture.h"
#include "config.h"
#include "led/led_devices.h"
#include "light_button_config.h"
#include "panel_widget_config.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using namespace helix;

namespace {

nlohmann::json entry(const std::string& id, bool placed, nlohmann::json config = nullptr) {
    nlohmann::json e = {
        {"id", id},     {"enabled", placed}, {"col", placed ? 0 : -1}, {"row", placed ? 0 : -1},
        {"colspan", 2}, {"rowspan", 2}};
    if (!config.is_null()) {
        e["config"] = config;
    }
    return e;
}

PanelWidgetConfig load_panel(const std::string& panel_id, const nlohmann::json& widgets) {
    auto* cfg = Config::get_instance();
    // Multi-page format, matching test_widget_catalog_placement.cpp's make_layout():
    // a flat array with no entry holding a grid position reads as a pre-grid
    // config and is replaced wholesale by PanelWidgetConfig::load(), which would
    // mask exactly the placement states these tests construct.
    cfg->set<nlohmann::json>(cfg->df() + "panel_widgets/" + panel_id,
                             {{"main_page_index", 0},
                              {"next_page_id", 1},
                              {"pages", {{{"id", "main"}, {"widgets", widgets}}}}});
    PanelWidgetConfig pc(panel_id, *cfg);
    pc.load();
    return pc;
}

} // namespace

TEST_CASE_METHOD(HelixTestFixture, "home_light_button_keys: a configured button reads its key",
                 "[led][light_button]") {
    auto pc = load_panel("t_lb_keys",
                         nlohmann::json::array({entry("led", true, {{"led", "neopixel a"}})}));
    CHECK(home_light_button_keys(pc, "all") == std::vector<std::string>{"neopixel a"});
}

TEST_CASE_METHOD(HelixTestFixture, "home_light_button_keys: an unconfigured button reads pending",
                 "[led][light_button]") {
    auto pc = load_panel("t_lb_pending", nlohmann::json::array({entry("led", true)}));
    CHECK(home_light_button_keys(pc, "all") == std::vector<std::string>{"all"});
    CHECK(home_light_button_keys(pc, "") == std::vector<std::string>{""});
}

TEST_CASE_METHOD(HelixTestFixture, "home_light_button_keys: an unplaced button does not count",
                 "[led][light_button]") {
    auto pc = load_panel("t_lb_unplaced", nlohmann::json::array({entry("led", false)}));
    CHECK(home_light_button_keys(pc, "all").empty());
}

TEST_CASE_METHOD(HelixTestFixture, "adopt_pending_light_button: fills unset buttons once",
                 "[led][light_button]") {
    auto* cfg = Config::get_instance();
    auto pc = load_panel("t_lb_adopt", nlohmann::json::array({entry("led", true)}));
    cfg->set(cfg->df() + led::LIGHT_BUTTON_PENDING_PATH, std::string("neopixel a"));

    CHECK(adopt_pending_light_button(*cfg, pc));
    CHECK(pc.get_widget_config("led")["led"] == "neopixel a");
    CHECK(cfg->try_get_json(cfg->df() + led::LIGHT_BUTTON_PENDING_PATH)->is_null());
    CHECK_FALSE(adopt_pending_light_button(*cfg, pc));
}

TEST_CASE_METHOD(HelixTestFixture, "adopt_pending_light_button: never overwrites a choice",
                 "[led][light_button]") {
    auto* cfg = Config::get_instance();
    auto pc = load_panel("t_lb_keep",
                         nlohmann::json::array({entry("led", true, {{"led", "neopixel b"}})}));
    cfg->set(cfg->df() + led::LIGHT_BUTTON_PENDING_PATH, std::string("all"));
    CHECK_FALSE(adopt_pending_light_button(*cfg, pc));
    CHECK(pc.get_widget_config("led")["led"] == "neopixel b");
}

TEST_CASE_METHOD(HelixTestFixture,
                 "adopt_pending_light_button: clears pending even with no light buttons on "
                 "the home layout",
                 "[led][light_button]") {
    auto* cfg = Config::get_instance();
    auto pc = load_panel("t_lb_nobuttons", nlohmann::json::array({}));
    cfg->set(cfg->df() + led::LIGHT_BUTTON_PENDING_PATH, std::string("neopixel a"));

    CHECK_FALSE(adopt_pending_light_button(*cfg, pc));
    CHECK(cfg->try_get_json(cfg->df() + led::LIGHT_BUTTON_PENDING_PATH)->is_null());

    // A button added after the pass sees no pending value left to adopt, so it
    // reads as the chamber-light default, not the stale staged value.
    auto pc2 = load_panel("t_lb_nobuttons", nlohmann::json::array({entry("led", true)}));
    CHECK(home_light_button_keys(pc2, "") == std::vector<std::string>{""});
}

TEST_CASE_METHOD(HelixTestFixture,
                 "adopt_pending_light_button: two placed buttons, only the unset one adopts",
                 "[led][light_button]") {
    auto* cfg = Config::get_instance();
    nlohmann::json second = entry("led:1", true, {{"led", "neopixel b"}});
    second["col"] = 2;
    auto pc = load_panel("t_lb_two", nlohmann::json::array({entry("led", true), second}));
    CHECK(home_light_button_keys(pc, "all") == std::vector<std::string>{"all", "neopixel b"});

    cfg->set(cfg->df() + led::LIGHT_BUTTON_PENDING_PATH, std::string("neopixel a"));
    CHECK(adopt_pending_light_button(*cfg, pc));
    CHECK(pc.get_widget_config("led")["led"] == "neopixel a");
    CHECK(pc.get_widget_config("led:1")["led"] == "neopixel b");
}
