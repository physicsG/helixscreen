// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "live_sweep_preview.h"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using namespace helix::calibration;

namespace {

ResonanceTesterConfig chips(const char* chip, const char* chip_x, const char* chip_y) {
    ResonanceTesterConfig cfg;
    cfg.accel_chip = chip;
    cfg.accel_chip_x = chip_x;
    cfg.accel_chip_y = chip_y;
    return cfg;
}

void check_plan(const std::optional<SweepPlan>& plan, float start, float end, float rate) {
    REQUIRE(plan.has_value());
    CHECK(plan->freq_start_hz == start);
    CHECK(plan->freq_end_hz == end);
    CHECK(plan->hz_per_sec == rate);
}

LiveSweepFacts reachable_facts() {
    LiveSweepFacts facts;
    facts.socket_path = "/tmp/k.sock";
    facts.socket_reachable = true;
    facts.cfg.accel_chip = "adxl345";
    return facts;
}

/// Points 5..135 Hz with psd_x = f, psd_y = 2f, psd_z = 0, all clean.
std::vector<SweepPoint> ramp_points() {
    std::vector<SweepPoint> points;
    for (int k = 0; k <= 130; ++k) {
        const auto f = static_cast<float>(5 + k);
        points.push_back(SweepPoint{f, f, 2.0f * f, 0.0f, true});
    }
    return points;
}

} // namespace

TEST_CASE("accel_chip_for_sweep picks the chip that records the motion",
          "[sweep_preview][target]") {
    const auto one = chips("adxl345", "", "");
    CHECK(accel_chip_for_sweep(one, SweepAxis::X) == "adxl345");
    CHECK(accel_chip_for_sweep(one, SweepAxis::Y) == "adxl345");
    CHECK(accel_chip_for_sweep(one, SweepAxis::DIAGONAL) == "adxl345");

    const auto split = chips("", "adxl345 x", "adxl345 y");
    CHECK(accel_chip_for_sweep(split, SweepAxis::X) == "adxl345 x");
    CHECK(accel_chip_for_sweep(split, SweepAxis::Y) == "adxl345 y");
    CHECK_FALSE(accel_chip_for_sweep(split, SweepAxis::DIAGONAL).has_value());

    const auto x_only = chips("adxl345", "lis2dw", "");
    CHECK(accel_chip_for_sweep(x_only, SweepAxis::X) == "lis2dw");
    CHECK(accel_chip_for_sweep(x_only, SweepAxis::Y) == "adxl345");
    CHECK_FALSE(accel_chip_for_sweep(x_only, SweepAxis::DIAGONAL).has_value());

    const auto same = chips("", "adxl345 hotend", "adxl345 hotend");
    CHECK(accel_chip_for_sweep(same, SweepAxis::DIAGONAL) == "adxl345 hotend");

    const auto none = chips("", "", "");
    CHECK_FALSE(accel_chip_for_sweep(none, SweepAxis::X).has_value());
    CHECK_FALSE(accel_chip_for_sweep(none, SweepAxis::Y).has_value());
    CHECK_FALSE(accel_chip_for_sweep(none, SweepAxis::DIAGONAL).has_value());

    CHECK(accel_chip_for_sweep(chips("  adxl345  ", "", ""), SweepAxis::X) == "adxl345");
}

TEST_CASE("sweep_plan_for sends the window the way Klipper sweeps it", "[sweep_preview][target]") {
    ResonanceTesterConfig cfg;
    check_plan(sweep_plan_for(cfg, std::nullopt), 5.0f, 135.0f, 1.0f);
    check_plan(sweep_plan_for(cfg, BeltSweepWindow{58.4f, 72.6f}), 58.0f, 73.0f, 1.0f);

    ResonanceTesterConfig klipper;
    klipper.max_freq = 133.33f;
    check_plan(sweep_plan_for(klipper, std::nullopt), 5.0f, 133.33f, 1.0f);

    ResonanceTesterConfig stopped;
    stopped.hz_per_sec = 0.0f;
    CHECK_FALSE(sweep_plan_for(stopped, std::nullopt).has_value());

    CHECK_FALSE(sweep_plan_for(cfg, BeltSweepWindow{80.0f, 70.0f}).has_value());
    CHECK_FALSE(sweep_plan_for(cfg, BeltSweepWindow{0.4f, 10.0f}).has_value());
}

TEST_CASE("live_sweep_target is the one gate", "[sweep_preview][target]") {
    const auto facts = reachable_facts();

    const auto x = live_sweep_target(facts, SweepAxis::X);
    REQUIRE(x.has_value());
    CHECK(x->socket_path == "/tmp/k.sock");
    CHECK(x->accel_chip == "adxl345");
    CHECK(x->plan.freq_start_hz == 5.0f);
    CHECK(x->plan.freq_end_hz == 135.0f);
    CHECK(x->plan.hz_per_sec == 1.0f);

    const auto belt = live_sweep_target(facts, SweepAxis::DIAGONAL, BeltSweepWindow{58.4f, 72.6f});
    REQUIRE(belt.has_value());
    CHECK(belt->plan.freq_start_hz == 58.0f);
    CHECK(belt->plan.freq_end_hz == 73.0f);

    auto unreachable = facts;
    unreachable.socket_reachable = false;
    CHECK_FALSE(live_sweep_target(unreachable, SweepAxis::X).has_value());

    auto no_path = facts;
    no_path.socket_path = "";
    CHECK_FALSE(live_sweep_target(no_path, SweepAxis::X).has_value());

    auto no_chip = facts;
    no_chip.cfg.accel_chip = "";
    CHECK_FALSE(live_sweep_target(no_chip, SweepAxis::X).has_value());

    auto stopped = facts;
    stopped.cfg.hz_per_sec = 0.0f;
    CHECK_FALSE(live_sweep_target(stopped, SweepAxis::X).has_value());
}

TEST_CASE("layout_preview places points on the chart's axis", "[sweep_preview][target]") {
    const auto points = ramp_points();

    SECTION("XYZ over the sweep's own range") {
        const auto s = layout_preview(points, SweepColumn::XYZ, 5.0f, 135.0f);
        REQUIRE(s.freqs.size() == 131);
        REQUIRE(s.amps.size() == 131);
        CHECK(s.freqs.front() == 5.0f);
        CHECK(s.freqs.back() == 135.0f);
        for (size_t i = 0; i < s.amps.size(); ++i) {
            INFO(i);
            CHECK(s.amps[i] == 3.0f * static_cast<float>(5 + i));
        }
        CHECK(s.max_amp == 405.0f);
    }

    SECTION("X over a wider axis leaves NaN where nothing was swept") {
        const auto s = layout_preview(points, SweepColumn::X, 0.0f, 200.0f);
        REQUIRE(s.amps.size() == 201);
        for (size_t i = 0; i < 5; ++i) {
            CHECK(std::isnan(s.amps[i]));
        }
        CHECK(s.amps[5] == 5.0f);
        CHECK(s.amps[135] == 135.0f);
        for (size_t i = 136; i <= 200; ++i) {
            CHECK(std::isnan(s.amps[i]));
        }
        CHECK(s.max_amp == 135.0f);
    }

    SECTION("an off-grid axis takes the nearest point") {
        const auto a = layout_preview(points, SweepColumn::X, 5.2f, 134.2f);
        REQUIRE(a.amps.size() == 130);
        for (size_t i = 0; i < a.amps.size(); ++i) {
            INFO(i);
            CHECK(a.amps[i] == static_cast<float>(5 + i));
        }
        const auto b = layout_preview(points, SweepColumn::X, 5.6f, 134.6f);
        REQUIRE_FALSE(b.amps.empty());
        CHECK(b.amps[0] == 6.0f);
    }

    SECTION("a slot exactly between two points takes the lower one") {
        const auto s = layout_preview(points, SweepColumn::X, 5.5f, 135.5f);
        REQUIRE(s.amps.size() == 131);
        for (size_t i = 0; i < s.amps.size(); ++i) {
            INFO(i);
            CHECK(s.amps[i] == static_cast<float>(5 + i));
        }
    }

    SECTION("an unclean point is a gap") {
        auto with_gap = points;
        with_gap[10].clean = false; // 15 Hz
        const auto s = layout_preview(with_gap, SweepColumn::XYZ, 5.0f, 135.0f);
        REQUIRE(s.amps.size() == 131);
        CHECK(std::isnan(s.amps[10]));
        CHECK(s.amps[9] == 42.0f);
        CHECK(s.amps[11] == 48.0f);
    }

    SECTION("the Y column") {
        const auto s = layout_preview(points, SweepColumn::Y, 5.0f, 135.0f);
        REQUIRE(s.amps.size() == 131);
        for (size_t i = 0; i < s.amps.size(); ++i) {
            INFO(i);
            CHECK(s.amps[i] == 2.0f * static_cast<float>(5 + i));
        }
    }

    SECTION("no points, or no axis") {
        const auto empty = layout_preview({}, SweepColumn::XYZ, 5.0f, 135.0f);
        REQUIRE(empty.amps.size() == 131);
        for (float a : empty.amps) {
            CHECK(std::isnan(a));
        }
        CHECK(empty.max_amp == 0.0f);

        CHECK(layout_preview(points, SweepColumn::XYZ, 135.0f, 135.0f).amps.empty());
        CHECK(layout_preview(points, SweepColumn::XYZ, 135.0f, 5.0f).freqs.empty());
        CHECK(layout_preview(points, SweepColumn::XYZ, NAN, 135.0f).amps.empty());
        CHECK(layout_preview(points, SweepColumn::XYZ, 5.0f, INFINITY).amps.empty());
    }
}

TEST_CASE("parse_klippy_uds_address reads /server/config", "[sweep_preview][target]") {
    using nlohmann::json;
    CHECK(parse_klippy_uds_address(json::parse(
              R"({"result":{"config":{"server":{"klippy_uds_address":"/tmp/k"}}}})")) == "/tmp/k");
    CHECK(parse_klippy_uds_address(
              json::parse(R"({"config":{"server":{"klippy_uds_address":"/tmp/k"}}})")) == "/tmp/k");
    CHECK(parse_klippy_uds_address(json::parse(R"({"config":{"server":{}}})")).empty());
    CHECK(parse_klippy_uds_address(json::parse(R"({"config":{"server":{"klippy_uds_address":7}}})"))
              .empty());
    CHECK(parse_klippy_uds_address(json::parse(R"(["/tmp/k"])")).empty());
    CHECK(parse_klippy_uds_address(json("/tmp/k")).empty());
}
