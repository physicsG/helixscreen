// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "resonance_console.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using namespace helix::calibration;

TEST_CASE("testing-frequency lines parse", "[resonance_console]") {
    CHECK(parse_testing_frequency("Testing frequency 74 Hz") == Catch::Approx(74.0f));
    CHECK(parse_testing_frequency("// Testing frequency 5 Hz") == Catch::Approx(5.0f));
    CHECK_FALSE(parse_testing_frequency("Wait for calculations..").has_value());
}

TEST_CASE("written-to path parses for both Klipper wordings and axis formats",
          "[resonance_console]") {
    CHECK(parse_written_csv_path("Resonances data written to "
                                 "/tmp/resonances_axis=1.000,1.000,0.000_helix_belt_a.csv file")
              .value() == "/tmp/resonances_axis=1.000,1.000,0.000_helix_belt_a.csv");
    CHECK(parse_written_csv_path(
              "Resonances data written to /tmp/resonances_axis=1.000,-1.000_helix_belt_b.csv file")
              .value() == "/tmp/resonances_axis=1.000,-1.000_helix_belt_b.csv");
    CHECK(parse_written_csv_path(
              "Shaper calibration data written to /tmp/calibration_data_x_20260928.csv file")
              .value() == "/tmp/calibration_data_x_20260928.csv");
    CHECK_FALSE(parse_written_csv_path("Testing frequency 74 Hz").has_value());
}

TEST_CASE("resonance_tester config: numbers, strings, missing keys", "[resonance_console]") {
    auto cfg = parse_resonance_tester_config(nlohmann::json::parse(
        R"({"resonance_tester": {"min_freq": 1, "max_freq": "100", "hz_per_sec": 2}})"));
    CHECK(cfg.from_printer);
    CHECK(cfg.min_freq == Catch::Approx(1.0f));
    CHECK(cfg.max_freq == Catch::Approx(100.0f));
    CHECK(cfg.hz_per_sec == Catch::Approx(2.0f));
    CHECK(cfg.sweep_seconds() == Catch::Approx(49.5f));

    auto none = parse_resonance_tester_config(nlohmann::json::object());
    CHECK_FALSE(none.from_printer);
    CHECK(none.sweep_seconds() == Catch::Approx(130.0f));
}

TEST_CASE("sweep percent follows the configured range", "[resonance_console]") {
    ResonanceTesterConfig cfg;
    cfg.min_freq = 1.0f;
    cfg.max_freq = 101.0f;
    CHECK(sweep_percent(51.0f, cfg) == 50);
    CHECK(sweep_percent(0.0f, cfg) == 0);
    CHECK(sweep_percent(500.0f, cfg) == 100);
}

TEST_CASE("resonance_tester config reads the first probe point", "[resonance_console][settle]") {
    const auto parse = [](const char* text) {
        return parse_resonance_tester_config(nlohmann::json::parse(text));
    };

    SECTION("configfile's nested array") {
        auto cfg = parse(R"({"resonance_tester": {"probe_points": [[175.0, 175.0, 20.0]]}})");
        REQUIRE(cfg.probe_point.has_value());
        CHECK((*cfg.probe_point)[0] == Catch::Approx(175.0f));
        CHECK((*cfg.probe_point)[1] == Catch::Approx(175.0f));
        CHECK((*cfg.probe_point)[2] == Catch::Approx(20.0f));
    }
    SECTION("a string, first line only") {
        auto cfg = parse(R"({"resonance_tester": {"probe_points": "175, 175, 20\n10, 10, 5"}})");
        REQUIRE(cfg.probe_point.has_value());
        CHECK((*cfg.probe_point)[0] == Catch::Approx(175.0f));
        CHECK((*cfg.probe_point)[1] == Catch::Approx(175.0f));
        CHECK((*cfg.probe_point)[2] == Catch::Approx(20.0f));
    }
    SECTION("two coordinates are not a point, and keep the range") {
        auto cfg = parse(R"({"resonance_tester": {"probe_points": [[1, 2]], "max_freq": 100}})");
        CHECK_FALSE(cfg.probe_point.has_value());
        CHECK(cfg.from_printer);
        CHECK(cfg.max_freq == Catch::Approx(100.0f));
    }
    SECTION("an unparseable string") {
        auto cfg = parse(R"({"resonance_tester": {"probe_points": "abc"}})");
        CHECK_FALSE(cfg.probe_point.has_value());
        CHECK(cfg.from_printer);
    }
    SECTION("absent") {
        auto cfg = parse(R"({"resonance_tester": {"min_freq": 5}})");
        CHECK_FALSE(cfg.probe_point.has_value());
        CHECK(cfg.from_printer);
    }
}

TEST_CASE("resonance_tester config reads the accelerometer chips",
          "[resonance_console][sweep_preview]") {
    const auto parse = [](const char* text) {
        return parse_resonance_tester_config(nlohmann::json::parse(text));
    };

    auto one = parse(R"({"resonance_tester": {"accel_chip": "adxl345"}})");
    CHECK(one.accel_chip == "adxl345");
    CHECK(one.accel_chip_x.empty());
    CHECK(one.accel_chip_y.empty());

    auto split = parse(
        R"({"resonance_tester": {"accel_chip_x": " adxl345 hotend ", "accel_chip_y": "lis2dw bed"}})");
    CHECK(split.accel_chip.empty());
    CHECK(split.accel_chip_x == "adxl345 hotend");
    CHECK(split.accel_chip_y == "lis2dw bed");

    auto number = parse(R"({"resonance_tester": {"accel_chip": 7}})");
    CHECK(number.from_printer);
    CHECK(number.accel_chip.empty());

    auto none = parse_resonance_tester_config(nlohmann::json::object());
    CHECK(none.accel_chip.empty());
    CHECK(none.accel_chip_x.empty());
    CHECK(none.accel_chip_y.empty());

    auto malformed =
        parse(R"({"resonance_tester": {"min_freq": "fast", "accel_chip": "adxl345"}})");
    CHECK_FALSE(malformed.from_printer);
    CHECK(malformed.accel_chip.empty());
}
