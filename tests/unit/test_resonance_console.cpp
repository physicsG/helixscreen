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
