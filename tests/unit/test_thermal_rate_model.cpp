// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../include/thermal_rate_model.h"
#include "preprint_predictor.h"
#include "printer_state.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("ThermalRateModel basic rate measurement", "[thermal_rate]") {
    ThermalRateModel model;
    model.reset(25.0f);

    // First sample establishes timing baseline
    model.record_sample(25.0f, 1000);

    // Small delta (< 2°C) — no measurement yet
    model.record_sample(26.5f, 2000);
    REQUIRE_FALSE(model.measured_rate().has_value());

    // 2°C delta from last recorded but total movement < 5°C — still no seed
    model.record_sample(28.0f, 4000);
    REQUIRE_FALSE(model.measured_rate().has_value());

    // Now at 31°C — total movement >= 5°C from start (25), seed from cumulative
    // 31 - 25 = 6°C in (8000 - 1000) = 7000ms = 7s → 7/6 ≈ 1.167 s/°C
    model.record_sample(31.0f, 8000);
    REQUIRE(model.measured_rate().has_value());
    float rate = model.measured_rate().value();
    REQUIRE(rate > 1.0f);
    REQUIRE(rate < 1.3f);
}

TEST_CASE("ThermalRateModel EMA smoothing", "[thermal_rate]") {
    ThermalRateModel model;
    model.reset(25.0f);

    // Establish baseline and seed measurement
    model.record_sample(25.0f, 1000);
    // Jump to 31°C in 6s → cumulative rate = 6/6 = 1.0 s/°C
    model.record_sample(31.0f, 7000);
    REQUIRE(model.measured_rate().has_value());
    float seeded = model.measured_rate().value();
    REQUIRE(seeded == Catch::Approx(1.0f).margin(0.05f));

    // Next sample: 33°C at 9s → inst_rate = 2000ms/1000/2°C = 1.0 s/°C
    // EMA: 0.3*1.0 + 0.7*1.0 = 1.0 (no change when rates match)
    model.record_sample(33.0f, 9000);
    REQUIRE(model.measured_rate().value() == Catch::Approx(1.0f).margin(0.05f));

    // Sudden fast rate: 35°C at 10s → inst_rate = 1000/1000/2 = 0.5 s/°C
    // EMA: 0.3*0.5 + 0.7*1.0 = 0.85 (damped, doesn't jump to 0.5)
    model.record_sample(35.0f, 10000);
    float after_fast = model.measured_rate().value();
    REQUIRE(after_fast > 0.8f);
    REQUIRE(after_fast < 0.9f);

    // Sudden slow rate: 37°C at 14s → inst_rate = 4000/1000/2 = 2.0 s/°C
    // EMA: 0.3*2.0 + 0.7*0.85 = 1.195
    model.record_sample(37.0f, 14000);
    float after_slow = model.measured_rate().value();
    REQUIRE(after_slow > 1.1f);
    REQUIRE(after_slow < 1.3f);
}

TEST_CASE("ThermalRateModel estimate_seconds", "[thermal_rate]") {
    ThermalRateModel model;
    model.reset(25.0f);

    // Seed at 1.0 s/°C
    model.record_sample(25.0f, 1000);
    model.record_sample(31.0f, 7000);

    // 50 degrees remaining at 1.0 s/°C = 50 seconds
    float eta = model.estimate_seconds(150.0f, 200.0f);
    REQUIRE(eta == Catch::Approx(50.0f).margin(1.0f));

    // Already at target — 0 seconds
    float at_target = model.estimate_seconds(200.0f, 200.0f);
    REQUIRE(at_target == 0.0f);

    // Above target — 0 seconds
    float above = model.estimate_seconds(210.0f, 200.0f);
    REQUIRE(above == 0.0f);
}

TEST_CASE("ThermalRateModel rate priority", "[thermal_rate]") {
    ThermalRateModel model;
    model.reset(25.0f);

    // Default only
    REQUIRE(model.best_rate() == Catch::Approx(ThermalRateModel::FALLBACK_DEFAULT_RATE));

    // Custom default
    model.set_default_rate(1.5f);
    REQUIRE(model.best_rate() == Catch::Approx(1.5f));

    // History overrides default
    model.load_history(2.0f);
    REQUIRE(model.best_rate() == Catch::Approx(2.0f));

    // Measured overrides history
    model.record_sample(25.0f, 1000);
    model.record_sample(31.0f, 7000); // seed: 1.0 s/°C
    REQUIRE(model.best_rate() == Catch::Approx(1.0f).margin(0.05f));
}

TEST_CASE("ThermalRateModel blended_rate_for_save", "[thermal_rate]") {
    ThermalRateModel model;
    model.reset(25.0f);

    // Seed measurement at 1.0 s/°C
    model.record_sample(25.0f, 1000);
    model.record_sample(31.0f, 7000);

    // No history — returns measured directly
    float no_hist = model.blended_rate_for_save();
    REQUIRE(no_hist == Catch::Approx(1.0f).margin(0.05f));

    // With history: 0.7 * measured + 0.3 * history
    model.load_history(2.0f);
    float blended = model.blended_rate_for_save();
    // 0.7 * 1.0 + 0.3 * 2.0 = 1.3
    REQUIRE(blended == Catch::Approx(1.3f).margin(0.05f));
}

TEST_CASE("ThermalRateModel leaves a hold between two climbs out of the rate it keeps",
          "[thermal_rate]") {
    // A nozzle heated to a probing temperature, held there for five minutes,
    // then heated to print temperature: 0.5 s/C on both climbs, sampled every
    // 5s as the pre-print collector does.
    ThermalRateModel model;
    model.reset(30.0f);
    uint32_t tick = 5000;
    float temp = 30.0f;
    for (; temp < 130.0f; tick += 5000, temp += 10.0f) {
        model.record_sample(temp, tick);
    }
    for (int i = 0; i < 60; ++i, tick += 5000) {
        model.record_sample(130.0f + ((i % 2 == 0) ? 0.3f : -0.3f), tick);
    }
    for (temp = 140.0f; temp <= 230.0f; tick += 5000, temp += 10.0f) {
        model.record_sample(temp, tick);
    }

    CAPTURE(model.blended_rate_for_save(), model.measured_rate().value_or(-1.0f));
    REQUIRE(model.blended_rate_for_save() == Catch::Approx(0.5f).margin(0.1f));
    REQUIRE(model.measured_rate().value_or(0.0f) == Catch::Approx(0.5f).margin(0.1f));
}

TEST_CASE("ThermalRateModel keeps the rate of the whole climb, not its last approach",
          "[thermal_rate]") {
    // A bed climbs 60C at 0.5 s/C, then crawls its last 10C at 5 s/C: 80s for
    // 70C over the whole climb.
    ThermalRateModel model;
    model.reset(30.0f);
    uint32_t tick = 5000;
    for (float temp = 30.0f; temp <= 90.0f; tick += 5000, temp += 10.0f) {
        model.record_sample(temp, tick);
    }
    tick += 5000;
    for (float temp = 92.0f; temp <= 100.0f; tick += 10000, temp += 2.0f) {
        model.record_sample(temp, tick);
    }

    CAPTURE(model.blended_rate_for_save());
    REQUIRE(model.blended_rate_for_save() == Catch::Approx(80.0f / 70.0f).margin(0.15f));
}

TEST_CASE("ThermalRateModel no measurement returns 0 for save", "[thermal_rate]") {
    ThermalRateModel model;
    model.reset(25.0f);

    // No measurements taken
    REQUIRE(model.blended_rate_for_save() == 0.0f);

    // Even with history, no measurement means nothing to save
    model.load_history(2.0f);
    REQUIRE(model.blended_rate_for_save() == 0.0f);
}

// --- ThermalRateManager tests ---

TEST_CASE("ThermalRateManager get_model returns per-heater instances", "[thermal_rate]") {
    ThermalRateManager manager;
    auto& extruder = manager.get_model("extruder");
    auto& bed = manager.get_model("heater_bed");
    REQUIRE(&extruder != &bed);
    auto& extruder2 = manager.get_model("extruder");
    REQUIRE(&extruder == &extruder2);
}

TEST_CASE("ThermalRateManager estimate_heating_seconds", "[thermal_rate]") {
    ThermalRateManager manager;
    manager.get_model("extruder").set_default_rate(0.5f);
    manager.get_model("heater_bed").set_default_rate(1.5f);
    float ext = manager.estimate_heating_seconds("extruder", 25.0f, 200.0f);
    REQUIRE(ext == Catch::Approx(87.5f));
    float bed = manager.estimate_heating_seconds("heater_bed", 25.0f, 100.0f);
    REQUIRE(bed == Catch::Approx(112.5f));
    REQUIRE(manager.estimate_heating_seconds("extruder", 200.0f, 200.0f) == Catch::Approx(0.0f));
}

TEST_CASE("ThermalRateManager apply_archetype_defaults", "[thermal_rate]") {
    ThermalRateManager manager;
    manager.apply_archetype_defaults(350.0f, "");
    REQUIRE(manager.get_model("extruder").best_rate() == Catch::Approx(0.25f));
    REQUIRE(manager.get_model("heater_bed").best_rate() == Catch::Approx(2.0f));

    ThermalRateManager manager2;
    manager2.apply_archetype_defaults(235.0f, "");
    REQUIRE(manager2.get_model("heater_bed").best_rate() == Catch::Approx(1.0f));
}

TEST_CASE("ThermalRateManager takes measured rates from the printer database", "[thermal_rate]") {
    // A 256mm bed reads as a medium bed (1.5 s/C) to the size guess.
    constexpr float BED_X_MAX = 256.0f;

    SECTION("Centauri Carbon: 24.5 to 105C took ~484s on the printer") {
        ThermalRateManager manager;
        manager.apply_archetype_defaults(BED_X_MAX, "Elegoo Centauri Carbon");
        REQUIRE(manager.estimate_heating_seconds("heater_bed", 24.5f, 105.0f) >= 400.0f);
        REQUIRE(manager.get_model("heater_bed").best_rate() == Catch::Approx(6.0f));
        REQUIRE(manager.get_model("extruder").best_rate() == Catch::Approx(0.4f));
    }

    SECTION("A printer without measured rates keeps the size guess") {
        ThermalRateManager manager;
        manager.apply_archetype_defaults(BED_X_MAX, "Voron 2.4");
        REQUIRE(manager.get_model("heater_bed").best_rate() == Catch::Approx(1.5f));
        REQUIRE(manager.get_model("extruder").best_rate() == Catch::Approx(0.25f));
    }

    SECTION("A rate learned on the printer still wins") {
        ThermalRateManager manager;
        manager.get_model("heater_bed").load_history(4.0f);
        manager.apply_archetype_defaults(BED_X_MAX, "Elegoo Centauri Carbon");
        REQUIRE(manager.get_model("heater_bed").best_rate() == Catch::Approx(4.0f));
    }
}

// ============================================================================
// Composite Remaining: thermal model + predictor defaults (no history)
// ============================================================================

TEST_CASE("ThermalRateManager composite estimate without predictor history", "[thermal_rate]") {
    // Simulates what the collector does: thermal model for heating + predictor defaults
    ThermalRateManager manager;
    manager.get_model("extruder").set_default_rate(0.5f);   // 0.5 s/°C
    manager.get_model("heater_bed").set_default_rate(1.5f); // 1.5 s/°C

    // Heating estimates: 25→200°C nozzle, 25→60°C bed
    float ext_heat = manager.estimate_heating_seconds("extruder", 25.0f, 200.0f);
    float bed_heat = manager.estimate_heating_seconds("heater_bed", 25.0f, 60.0f);
    REQUIRE(ext_heat == Catch::Approx(87.5f)); // 175°C * 0.5
    REQUIRE(bed_heat == Catch::Approx(52.5f)); // 35°C * 1.5

    // Predictor defaults (no history — has_predictions is false without entries)
    helix::PreprintPredictor predictor;
    REQUIRE_FALSE(predictor.has_predictions());
    auto defaults = predictor.predicted_phases();
    REQUIRE_FALSE(defaults.empty());

    // Composite total = heating + operation defaults
    float total = ext_heat + bed_heat;
    for (const auto& [phase, dur] : defaults) {
        total += static_cast<float>(dur);
    }
    // Should be heating (~140s) + defaults (homing=20 + mesh=90 + qgl=60 + z_tilt=45 + clean=15 +
    // purge=10 = 240)
    REQUIRE(total > 350.0f);
    REQUIRE(total < 420.0f);
}

TEST_CASE("ThermalRateManager composite estimate with predictor history", "[thermal_rate]") {
    ThermalRateManager manager;
    manager.get_model("extruder").set_default_rate(0.5f);
    manager.get_model("heater_bed").set_default_rate(1.5f);

    // Predictor WITH history — learned bed mesh takes 166s, not the 90s default
    helix::PreprintPredictor predictor;
    int mesh_phase = static_cast<int>(helix::PrintStartPhase::BED_MESH);
    int homing_phase = static_cast<int>(helix::PrintStartPhase::HOMING);
    predictor.load_entries({{186, 1700000000, {{homing_phase, 5}, {mesh_phase, 166}}}});

    REQUIRE(predictor.has_predictions());
    auto phases = predictor.predicted_phases();
    REQUIRE(phases[mesh_phase] == 166); // learned, not default 90

    // Learned bed mesh (166s) should be larger than default (90s)
    helix::PreprintPredictor predictor_default;
    auto defaults = predictor_default.predicted_phases();
    REQUIRE(phases[mesh_phase] > defaults[mesh_phase]);
}

TEST_CASE("ThermalRateManager heating estimate decreases as temp rises", "[thermal_rate]") {
    // Verifies the thermal model correctly reduces remaining as temperature increases
    ThermalRateManager manager;
    manager.get_model("heater_bed").set_default_rate(1.5f); // 1.5 s/°C

    float cold = manager.estimate_heating_seconds("heater_bed", 25.0f, 60.0f);      // 52.5s
    float warm = manager.estimate_heating_seconds("heater_bed", 45.0f, 60.0f);      // 22.5s
    float hot = manager.estimate_heating_seconds("heater_bed", 58.0f, 60.0f);       // 3.0s
    float at_target = manager.estimate_heating_seconds("heater_bed", 60.0f, 60.0f); // 0s

    REQUIRE(cold > warm);
    REQUIRE(warm > hot);
    REQUIRE(hot > at_target);
    REQUIRE(at_target == Catch::Approx(0.0f));
}
