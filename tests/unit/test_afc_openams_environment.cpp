// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_afc_openams_environment.cpp
 * @brief OpenAMS temperature_oams humidity reaches the per-unit environment
 *        model, attached to the unit whose `oams` option names the sensor (#1150).
 */

#include "../lvgl_test_fixture.h"
#include "ams_backend_afc.h"
#include "humidity_sensor_types.h"
#include "test_helpers/afc_test_access.h"
#include "test_helpers/registered_backend.h"

#include <map>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace helix {

class AfcOamsHelper : public AmsBackendAfc {
  public:
    explicit AfcOamsHelper(const std::vector<std::string>& names)
        : AmsBackendAfc(nullptr, nullptr) {
        AfcTestAccess::initialize_slots(*this, names);
    }

    void feed(const nlohmann::json& params) {
        nlohmann::json notification;
        notification["params"] = nlohmann::json::array({params, 0.0});
        handle_status_update(notification);
    }

    void feed_configfile(const std::map<std::string, std::string>& oams_by_unit) {
        nlohmann::json settings = nlohmann::json::object();
        for (const auto& [unit, oams] : oams_by_unit) {
            settings["afc_openams " + unit] = {{"oams", oams}};
        }
        const nlohmann::json response = {
            {"result", {{"status", {{"configfile", {{"settings", settings}}}}}}}};
        auto topo = parse_configfile_topology(response);
        std::lock_guard<std::mutex> lock(mutex_);
        AfcTestAccess::unit_oams_names(*this) = std::move(topo.oams_names);
        AfcTestAccess::apply_unit_environment(*this);
    }

    [[nodiscard]] std::optional<EnvironmentData> env(int unit) const {
        return get_system_info().units.at(static_cast<size_t>(unit)).environment;
    }
};

} // namespace helix

namespace {
nlohmann::json two_openams_units() {
    return {{"AFC", {{"units", {"OpenAMS AMS_1", "OpenAMS AMS_2"}}}},
            {"AFC_OpenAMS AMS_1", {{"lanes", {"lane1", "lane2"}}, {"hubs", {"Hub_1"}}}},
            {"AFC_OpenAMS AMS_2", {{"lanes", {"lane3", "lane4"}}, {"hubs", {"Hub_2"}}}}};
}
} // namespace

TEST_CASE("humidity chip table recognises the OpenAMS sensor objects", "[1150][humidity]") {
    using namespace helix::sensors;
    REQUIRE(humidity_chip_for_object("temperature_oams oams1") != nullptr);
    CHECK(humidity_chip_for_object("aht3x oams1")->type == HumiditySensorType::AHT3X);
}

TEST_CASE_METHOD(LVGLTestFixture, "OpenAMS sensors attach to the unit named by configfile (#1150)",
                 "[1150][ams][afc]") {
    helix::test::RegisteredBackend<AfcOamsHelper> reg{
        std::vector<std::string>{"lane1", "lane2", "lane3", "lane4"}};
    AfcOamsHelper& afc = *reg;
    afc.feed(two_openams_units());
    // Sensor names deliberately cross the unit order: AMS_1 owns oams2.
    afc.feed_configfile({{"ams_1", "oams2"}, {"ams_2", "oams1"}});

    afc.feed({{"aht3x oams1", {{"temperature", 31.5}, {"humidity", 18.0}}},
              {"temperature_oams oams2", {{"temperature", 24.0}, {"humidity", 44.0}}}});

    REQUIRE(afc.env(0).has_value());
    CHECK(afc.env(0)->humidity_pct == Catch::Approx(44.0f));
    CHECK(afc.env(0)->has_humidity);
    CHECK(afc.env(1)->humidity_pct == Catch::Approx(18.0f));
    CHECK(afc.env(1)->temperature_c == Catch::Approx(31.5f));
    CHECK(afc.traits().has_environment_sensors);
}

TEST_CASE_METHOD(LVGLTestFixture, "OpenAMS delta frames keep the other reading (#1150)",
                 "[1150][ams][afc]") {
    helix::test::RegisteredBackend<AfcOamsHelper> reg{
        std::vector<std::string>{"lane1", "lane2", "lane3", "lane4"}};
    AfcOamsHelper& afc = *reg;
    afc.feed(two_openams_units());
    afc.feed_configfile({{"ams_1", "oams1"}});

    afc.feed({{"temperature_oams oams1", {{"temperature", 24.0}, {"humidity", 44.0}}}});
    afc.feed({{"temperature_oams oams1", {{"temperature", 25.0}}}});

    CHECK(afc.env(0)->temperature_c == Catch::Approx(25.0f));
    CHECK(afc.env(0)->humidity_pct == Catch::Approx(44.0f));
    // The unit that no sensor names has none.
    CHECK_FALSE(afc.env(1).has_value());
}

TEST_CASE_METHOD(LVGLTestFixture, "A backend with no OpenAMS sensor offers no environment (#1150)",
                 "[1150][ams][afc]") {
    helix::test::RegisteredBackend<AfcOamsHelper> reg{
        std::vector<std::string>{"lane1", "lane2", "lane3", "lane4"}};
    AfcOamsHelper& afc = *reg;
    afc.feed(two_openams_units());
    CHECK_FALSE(afc.traits().has_environment_sensors);
    CHECK_FALSE(afc.env(0).has_value());
}

TEST_CASE_METHOD(LVGLTestFixture, "Sensor frames with no OpenAMS unit offer no environment (#1150)",
                 "[1150][ams][afc]") {
    helix::test::RegisteredBackend<AfcOamsHelper> reg{std::vector<std::string>{"lane1", "lane2"}};
    AfcOamsHelper& afc = *reg;
    afc.feed({{"AFC", {{"units", {"Box_Turtle Turtle_1"}}}},
              {"AFC_BoxTurtle Turtle_1", {{"lanes", {"lane1", "lane2"}}}}});
    afc.feed({{"aht3x oams1", {{"temperature", 24.0}, {"humidity", 44.0}}}});
    CHECK_FALSE(afc.traits().has_environment_sensors);
}

TEST_CASE_METHOD(LVGLTestFixture, "A lone OpenAMS unit gets its reading (#1150)",
                 "[1150][ams][afc]") {
    helix::test::RegisteredBackend<AfcOamsHelper> reg{std::vector<std::string>{"lane1", "lane2"}};
    AfcOamsHelper& afc = *reg;
    afc.feed({{"AFC", {{"units", {"OpenAMS AMS_1"}}}},
              {"AFC_OpenAMS AMS_1", {{"lanes", {"lane1", "lane2"}}, {"hubs", {"Hub_1"}}}}});
    afc.feed({{"temperature_oams oamsX", {{"temperature", 24.0}, {"humidity", 44.0}}}});
    afc.feed({{"AFC_OpenAMS AMS_1", {{"lanes", {"lane1", "lane2"}}}}});
    REQUIRE(afc.env(0).has_value());
    CHECK(afc.env(0)->humidity_pct == Catch::Approx(44.0f));
    CHECK(afc.traits().has_environment_sensors);
}
