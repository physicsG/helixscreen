// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_preprint_predictor.cpp
 * @brief Unit tests for PreprintPredictor weighted averages
 *
 * Tests pure prediction logic without LVGL or Config dependencies.
 */

#include "../test_helpers/preprint_config_scope.h"
#include "preprint_predictor.h"
#include "printer_state.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using helix::PreprintEntry;
using helix::PreprintPredictor;

// ============================================================================
// Empty State
// ============================================================================

TEST_CASE("PreprintPredictor: defaults without history", "[print][predictor]") {
    PreprintPredictor predictor;

    // Empty predictor has no predictions (entries-based)
    REQUIRE_FALSE(predictor.has_predictions());
    // But default phase durations are still available via predicted_phases()
    auto defaults = predictor.predicted_phases();
    REQUIRE_FALSE(defaults.empty());
    REQUIRE(predictor.predicted_total() > 0);
}

TEST_CASE("PreprintPredictor: homing estimate never drops below the floor", "[print][predictor]") {
    PreprintPredictor predictor;
    const int homing = static_cast<int>(PrintStartPhase::HOMING);

    SECTION("history that barely saw homing") {
        predictor.load_entries({{120, 1700000000, {{homing, 3}}}});
        REQUIRE(predictor.predicted_homing_seconds() == PreprintPredictor::kMinHomingSeconds);
    }

    SECTION("history with no homing phase at all") {
        predictor.load_entries(
            {{120, 1700000000, {{static_cast<int>(PrintStartPhase::BED_MESH), 60}}}});
        REQUIRE(predictor.predicted_homing_seconds() == PreprintPredictor::kMinHomingSeconds);
    }

    SECTION("history with a real homing time") {
        predictor.load_entries({{120, 1700000000, {{homing, 45}}}});
        REQUIRE(predictor.predicted_homing_seconds() == 45);
    }
}

TEST_CASE("PreprintPredictor: default_phase_durations returns expected phases",
          "[print][predictor]") {
    auto defaults = PreprintPredictor::default_phase_durations();
    REQUIRE(defaults.size() == 6);
    REQUIRE(defaults[static_cast<int>(PrintStartPhase::HOMING)] == 30);
    REQUIRE(defaults[static_cast<int>(PrintStartPhase::BED_MESH)] == 90);
    REQUIRE(defaults[static_cast<int>(PrintStartPhase::QGL)] == 60);
    REQUIRE(defaults[static_cast<int>(PrintStartPhase::Z_TILT)] == 45);
    REQUIRE(defaults[static_cast<int>(PrintStartPhase::CLEANING)] == 20);
    REQUIRE(defaults[static_cast<int>(PrintStartPhase::PURGING)] == 15);
}

// ============================================================================
// Single Entry
// ============================================================================

TEST_CASE("PreprintPredictor: single entry uses 100% weight", "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({{185, 1700000000, {{2, 25}, {3, 90}, {7, 30}, {9, 20}}}});

    REQUIRE(predictor.has_predictions());
    // predicted_total() returns wall-clock total_seconds (185), not phase sum.
    // The 20s gap vs phase sum (165) represents unmapped/heating time.
    REQUIRE(predictor.predicted_total() == 185);

    auto phases = predictor.predicted_phases();
    REQUIRE(phases[2] == 25);
    REQUIRE(phases[3] == 90);
    REQUIRE(phases[7] == 30);
    REQUIRE(phases[9] == 20);
}

// ============================================================================
// Two Entries (60/40 weighting)
// ============================================================================

TEST_CASE("PreprintPredictor: two entries favor newer entry", "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({
        {100, 1700000000, {{2, 20}}}, // older: lower weight
        {100, 1700000001, {{2, 30}}}, // newer: higher weight
    });

    auto phases = predictor.predicted_phases();
    // Exponential weighting: newer entry dominates
    // Prediction should be between 20 and 30, closer to 30
    REQUIRE(phases[2] > 24);
    REQUIRE(phases[2] <= 30);
}

// ============================================================================
// Three Entries (50/30/20 weighting)
// ============================================================================

TEST_CASE("PreprintPredictor: three entries favor newest", "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({
        {100, 1700000000, {{2, 10}}}, // oldest
        {100, 1700000001, {{2, 20}}}, // middle
        {100, 1700000002, {{2, 30}}}, // newest
    });

    auto phases = predictor.predicted_phases();
    // Exponential weighting: prediction closer to 30 than to 10
    REQUIRE(phases[2] > 18);
    REQUIRE(phases[2] <= 30);
}

// ============================================================================
// FIFO Trimming
// ============================================================================

TEST_CASE("PreprintPredictor: add_entry FIFO trims to MAX_ENTRIES", "[print][predictor]") {
    PreprintPredictor predictor;
    // Load MAX_ENTRIES entries
    std::vector<PreprintEntry> entries;
    for (int i = 0; i < PreprintPredictor::MAX_ENTRIES; i++) {
        entries.push_back({100, 1700000000 + i, {{2, 10 + i}}});
    }
    predictor.load_entries(entries);
    REQUIRE(predictor.get_entries().size() == static_cast<size_t>(PreprintPredictor::MAX_ENTRIES));

    // Add one more — oldest should be evicted
    predictor.add_entry({100, 1700000000 + PreprintPredictor::MAX_ENTRIES, {{2, 99}}});
    auto result = predictor.get_entries();
    REQUIRE(result.size() == static_cast<size_t>(PreprintPredictor::MAX_ENTRIES));
    // First entry should now be the second original one
    REQUIRE(result.front().phase_durations.at(2) == 11);
    // Last entry should be the newly added one
    REQUIRE(result.back().phase_durations.at(2) == 99);
}

// ============================================================================
// 15-Minute Cap
// ============================================================================

TEST_CASE("PreprintPredictor: add_entry accepts any duration (MAD handles outliers)",
          "[print][predictor]") {
    PreprintPredictor predictor;

    // Large entries are no longer rejected at add time — MAD handles anomalies in prediction
    predictor.add_entry({901, 1700000000, {{2, 500}}});
    REQUIRE(predictor.has_predictions());
    REQUIRE(predictor.get_entries().size() == 1);

    predictor.add_entry({900, 1700000001, {{2, 500}}});
    REQUIRE(predictor.get_entries().size() == 2);
}

// ============================================================================
// Phases That Appear in Only Some Entries
// ============================================================================

TEST_CASE("PreprintPredictor: phases appearing in subset of entries", "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({
        {100, 1700000000, {{2, 20}, {3, 80}}},           // has homing + heating
        {100, 1700000001, {{2, 25}}},                    // only homing
        {100, 1700000002, {{2, 30}, {3, 100}, {7, 40}}}, // homing + heating + mesh
    });

    auto phases = predictor.predicted_phases();

    // Phase 2 (homing): all three entries, newest=30 dominates
    REQUIRE(phases[2] >= 24);
    REQUIRE(phases[2] <= 30);

    // Phase 3 (heating): entries 0 and 2 only, newest=100 dominates
    REQUIRE(phases[3] >= 85);
    REQUIRE(phases[3] <= 100);

    // Phase 7 (mesh): only entry 2 — 100% of its weight
    REQUIRE(phases[7] == 40);
}

// ============================================================================
// Single Phase Entry
// ============================================================================

TEST_CASE("PreprintPredictor: single phase entry", "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({{30, 1700000000, {{3, 30}}}});

    REQUIRE(predictor.predicted_total() == 30);

    auto phases = predictor.predicted_phases();
    REQUIRE(phases.size() == 1);
    REQUIRE(phases[3] == 30);
}

// ============================================================================
// load_entries Replaces Existing
// ============================================================================

TEST_CASE("PreprintPredictor: load_entries replaces existing data", "[print][predictor]") {
    PreprintPredictor predictor;
    predictor.load_entries({{100, 1700000000, {{2, 50}}}});
    // predicted_total() returns wall-clock total_seconds, not phase sum
    REQUIRE(predictor.predicted_total() == 100);

    predictor.load_entries({{80, 1700000001, {{3, 30}}}});
    REQUIRE(predictor.predicted_total() == 80);

    auto phases = predictor.predicted_phases();
    REQUIRE(phases.count(2) == 0); // Old data gone
    REQUIRE(phases[3] == 30);
}

// ============================================================================
// load_entries Caps at MAX_ENTRIES
// ============================================================================

TEST_CASE("PreprintPredictor: load_entries caps at MAX_ENTRIES", "[print][predictor]") {
    PreprintPredictor predictor;
    std::vector<PreprintEntry> entries;
    for (int i = 0; i < PreprintPredictor::MAX_ENTRIES + 5; i++) {
        entries.push_back({100, 1700000000 + i, {{2, 10 + i}}});
    }
    predictor.load_entries(entries);

    // Should keep only the last MAX_ENTRIES
    REQUIRE(predictor.get_entries().size() == static_cast<size_t>(PreprintPredictor::MAX_ENTRIES));
}

// ============================================================================
// Temperature Bucket Filtering
// ============================================================================

TEST_CASE("PreprintPredictor: load_entries filters by temp_bucket", "[print][predictor]") {
    PreprintPredictor predictor;

    std::vector<PreprintEntry> all_entries = {
        {60, 1700000000, {{2, 10}, {4, 50}}, 200},   // PLA (200°C bucket)
        {120, 1700000001, {{2, 15}, {4, 105}}, 250}, // ASA (250°C bucket)
        {65, 1700000002, {{2, 12}, {4, 53}}, 200},   // PLA (200°C bucket)
    };

    SECTION("filter to 200°C bucket gets PLA entries only") {
        predictor.load_entries(all_entries, 200);
        REQUIRE(predictor.get_entries().size() == 2);
        REQUIRE(predictor.predicted_total() < 80); // ~63s average
    }

    SECTION("filter to 250°C bucket gets ASA entry only") {
        predictor.load_entries(all_entries, 250);
        REQUIRE(predictor.get_entries().size() == 1);
        REQUIRE(predictor.predicted_total() == 120);
    }

    SECTION("filter to unknown bucket gets nothing") {
        predictor.load_entries(all_entries, 225);
        REQUIRE(predictor.get_entries().empty());
    }

    SECTION("no filter (bucket=0) gets all entries") {
        predictor.load_entries(all_entries, 0);
        REQUIRE(predictor.get_entries().size() == 3);
    }
}

TEST_CASE("PreprintPredictor: legacy entries (bucket=0) match any filter", "[print][predictor]") {
    PreprintPredictor predictor;

    std::vector<PreprintEntry> entries = {
        {80, 1700000000, {{2, 10}}, 0},    // legacy (no bucket)
        {120, 1700000001, {{2, 15}}, 250}, // ASA bucket
    };

    // Legacy entry matches the 250 bucket filter
    predictor.load_entries(entries, 250);
    REQUIRE(predictor.get_entries().size() == 2);
}

TEST_CASE("PreprintPredictor: temp_bucket stored in entry", "[print][predictor]") {
    PreprintPredictor predictor;

    PreprintEntry entry;
    entry.total_seconds = 90;
    entry.timestamp = 1700000000;
    entry.phase_durations = {{2, 15}, {4, 75}};
    entry.temp_bucket = 275;

    predictor.add_entry(entry);

    auto entries = predictor.get_entries();
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].temp_bucket == 275);
}

// ============================================================================
// Time-Decay Weighting
// ============================================================================

TEST_CASE("PreprintPredictor time-decay weighting", "[preprint_predictor]") {
    PreprintPredictor predictor;
    std::vector<PreprintEntry> entries;
    int64_t base_time = 1700000000;
    int qgl_phase = static_cast<int>(PrintStartPhase::QGL);
    for (int i = 0; i < 5; i++) {
        PreprintEntry e;
        e.total_seconds = 60 + i * 10;
        e.timestamp = base_time + i * 3600;
        e.phase_durations[qgl_phase] = 60 + i * 10;
        entries.push_back(e);
    }
    predictor.load_entries(entries);
    auto phases = predictor.predicted_phases();
    REQUIRE(phases.count(qgl_phase) == 1);
    // Newest entries dominate — prediction should be closer to 100 than 60
    REQUIRE(phases[qgl_phase] > 80);
}

// ============================================================================
// MAD Anomaly Rejection
// ============================================================================

TEST_CASE("PreprintPredictor MAD anomaly rejection", "[preprint_predictor]") {
    PreprintPredictor predictor;
    int64_t base_time = 1700000000;
    std::vector<PreprintEntry> entries;
    int mesh_phase = static_cast<int>(PrintStartPhase::BED_MESH);
    for (int i = 0; i < 4; i++) {
        PreprintEntry e;
        e.total_seconds = 90;
        e.timestamp = base_time + i * 3600;
        e.phase_durations[mesh_phase] = 85 + i * 3;
        entries.push_back(e);
    }
    PreprintEntry anomaly;
    anomaly.total_seconds = 800;
    anomaly.timestamp = base_time + 5 * 3600;
    anomaly.phase_durations[mesh_phase] = 800;
    entries.push_back(anomaly);
    predictor.load_entries(entries);
    auto phases = predictor.predicted_phases();
    REQUIRE(phases[mesh_phase] < 120);
}

// ============================================================================
// Missing-Phase Normalization
// ============================================================================

TEST_CASE("PreprintPredictor missing-phase normalization", "[preprint_predictor]") {
    PreprintPredictor predictor;
    int64_t base_time = 1700000000;
    int mesh_phase = static_cast<int>(PrintStartPhase::BED_MESH);
    int qgl_phase = static_cast<int>(PrintStartPhase::QGL);
    PreprintEntry e1;
    e1.total_seconds = 150;
    e1.timestamp = base_time;
    e1.phase_durations[mesh_phase] = 90;
    e1.phase_durations[qgl_phase] = 60;
    PreprintEntry e2;
    e2.total_seconds = 60;
    e2.timestamp = base_time + 3600;
    e2.phase_durations[qgl_phase] = 55;
    predictor.load_entries({e1, e2});
    auto phases = predictor.predicted_phases();
    // Mesh only from e1
    REQUIRE(phases[mesh_phase] == 90);
    // QGL averages both
    REQUIRE(phases[qgl_phase] > 50);
    REQUIRE(phases[qgl_phase] < 65);
}

// ============================================================================
// Accepts Up to 10 Entries
// ============================================================================

TEST_CASE("PreprintPredictor accepts up to 10 entries", "[preprint_predictor]") {
    PreprintPredictor predictor;
    int64_t base_time = 1700000000;
    std::vector<PreprintEntry> entries;
    for (int i = 0; i < 12; i++) {
        PreprintEntry e;
        e.total_seconds = 100;
        e.timestamp = base_time + i * 3600;
        e.phase_durations[static_cast<int>(PrintStartPhase::HOMING)] = 20;
        entries.push_back(e);
    }
    predictor.load_entries(entries);
    REQUIRE(predictor.get_entries().size() == 10);
}

// ============================================================================
// Measurement-window bucketing
//
// The collector is armed either at the printer's own print-start edge (an
// externally started print) or at user commit (the screen started it). Commit
// arming puts any host-side pre-start block - a forced bed mesh, a setup macro
// - inside the measured window. On a printer that runs one, the two windows
// differ by minutes, so averaging them together produces an estimate that is
// wrong for both, and feeds a too-small predicted_total into the collector's
// adaptive timeout.
// ============================================================================

TEST_CASE("PreprintPredictor: host-pre-start window rejects printer-edge history",
          "[print][predictor]") {
    PreprintPredictor predictor;

    std::vector<PreprintEntry> entries = {
        // Externally started: window is the printer's own PRINT_START only.
        {300, 1700000000, {{2, 20}, {7, 100}}, 1, PreprintWindow::PrinterEdge},
        // Screen started on the same printer, with a host-side bed mesh in front.
        {1140, 1700000001, {{2, 20}, {7, 500}}, 1, PreprintWindow::HostPreStart},
    };

    predictor.load_entries(entries, 1, PreprintWindow::HostPreStart);

    auto kept = predictor.get_entries();
    REQUIRE(kept.size() == 1);
    REQUIRE(kept[0].total_seconds == 1140);
}

TEST_CASE("PreprintPredictor: printer-edge window rejects host-pre-start history",
          "[print][predictor]") {
    PreprintPredictor predictor;

    std::vector<PreprintEntry> entries = {
        {300, 1700000000, {{2, 20}, {7, 100}}, 1, PreprintWindow::PrinterEdge},
        {1140, 1700000001, {{2, 20}, {7, 500}}, 1, PreprintWindow::HostPreStart},
    };

    predictor.load_entries(entries, 1, PreprintWindow::PrinterEdge);

    auto kept = predictor.get_entries();
    REQUIRE(kept.size() == 1);
    REQUIRE(kept[0].total_seconds == 300);
}

TEST_CASE("PreprintPredictor: legacy entries count as printer-edge, not host-pre-start",
          "[print][predictor]") {
    // Every entry recorded before commit arming existed measured a printer-edge
    // window, so treating a missing field as PrinterEdge is a fact about the
    // data, not a guess. The asymmetry matters on upgrade: an existing history
    // stays usable for externally started prints, and cannot drag a host-side
    // block's estimate down to a duration that never included one.
    std::vector<PreprintEntry> entries = {
        {300, 1700000000, {{2, 20}}, 1, PreprintWindow::Unknown},
    };

    SECTION("a printer-edge query keeps them") {
        PreprintPredictor predictor;
        predictor.load_entries(entries, 1, PreprintWindow::PrinterEdge);
        REQUIRE(predictor.get_entries().size() == 1);
    }

    SECTION("a host-pre-start query discards them") {
        PreprintPredictor predictor;
        predictor.load_entries(entries, 1, PreprintWindow::HostPreStart);
        REQUIRE(predictor.get_entries().empty());
    }
}

TEST_CASE("PreprintPredictor: window filter composes with temp_bucket", "[print][predictor]") {
    PreprintPredictor predictor;

    std::vector<PreprintEntry> entries = {
        {1100, 1700000000, {{7, 500}}, 1, PreprintWindow::HostPreStart}, // cold + host
        {900, 1700000001, {{7, 400}}, 2, PreprintWindow::HostPreStart},  // warm + host
        {300, 1700000002, {{7, 100}}, 1, PreprintWindow::PrinterEdge},   // cold + edge
    };

    predictor.load_entries(entries, 1, PreprintWindow::HostPreStart);

    auto kept = predictor.get_entries();
    REQUIRE(kept.size() == 1);
    REQUIRE(kept[0].total_seconds == 1100);
}

TEST_CASE("PreprintPredictor: window survives add_entry round-trip", "[print][predictor]") {
    PreprintPredictor predictor;

    PreprintEntry entry;
    entry.total_seconds = 1140;
    entry.timestamp = 1700000000;
    entry.phase_durations = {{7, 500}};
    entry.temp_bucket = 1;
    entry.window = PreprintWindow::HostPreStart;

    predictor.add_entry(entry);

    auto kept = predictor.get_entries();
    REQUIRE(kept.size() == 1);
    REQUIRE(kept[0].window == PreprintWindow::HostPreStart);
}

TEST_CASE("PreprintPredictor: window defaults to Unknown when unspecified", "[print][predictor]") {
    PreprintEntry entry;
    REQUIRE(entry.window == PreprintWindow::Unknown);
}

TEST_CASE("PreprintPredictor: omitting the window filter keeps every entry", "[print][predictor]") {
    // Existing callers pass no window and must be unaffected.
    PreprintPredictor predictor;

    std::vector<PreprintEntry> entries = {
        {300, 1700000000, {{2, 20}}, 1, PreprintWindow::PrinterEdge},
        {1140, 1700000001, {{7, 500}}, 1, PreprintWindow::HostPreStart},
        {600, 1700000002, {{2, 30}}, 1, PreprintWindow::Unknown},
    };

    predictor.load_entries(entries, 1);

    REQUIRE(predictor.get_entries().size() == 3);
}

// ============================================================================
// estimate(): the model the countdown and the print details share
// ============================================================================

TEST_CASE("PreprintPredictor: estimate without history sums heating and default phases",
          "[print][predictor][estimate]") {
    PreprintConfigScope config;
    PreprintPredictor predictor;
    const int homing = static_cast<int>(PrintStartPhase::HOMING);
    const int nozzle = static_cast<int>(PrintStartPhase::HEATING_NOZZLE);
    const int bed = static_cast<int>(PrintStartPhase::HEATING_BED);
    const int mesh = static_cast<int>(PrintStartPhase::BED_MESH);

    float phases = 0.0f;
    for (const auto& [phase, secs] : PreprintPredictor::default_phase_durations()) {
        phases += phase == homing ? std::max(secs, PreprintPredictor::kMinHomingSeconds) : secs;
    }

    SECTION("heating sits on top of the phases") {
        const auto e = predictor.estimate({100.0f, 70.0f, true});
        CHECK(e.durations.at(nozzle) == 100.0f);
        CHECK(e.durations.at(bed) == 70.0f);
        CHECK(e.total_seconds == 170.0f + phases);
    }

    SECTION("an unknown nozzle target holds the placeholder") {
        const auto e = predictor.estimate({0.0f, 70.0f, false});
        CHECK(e.durations.at(nozzle) == PreprintPredictor::kUnknownNozzleHeatSeconds);
        CHECK(e.total_seconds == 70.0f + PreprintPredictor::kUnknownNozzleHeatSeconds + phases);
    }

    SECTION("a skipped phase leaves") {
        const auto e = predictor.estimate({0.0f, 0.0f, true}, {{mesh, false}});
        CHECK(e.durations.count(mesh) == 0);
        CHECK(e.total_seconds == phases - PreprintPredictor::default_phase_durations().at(mesh));
    }
}

TEST_CASE("PreprintPredictor: estimate with history takes the recorded wall clock",
          "[print][predictor][estimate]") {
    PreprintConfigScope config;
    const int homing = static_cast<int>(PrintStartPhase::HOMING);
    const int mesh = static_cast<int>(PrintStartPhase::BED_MESH);
    const int qgl = static_cast<int>(PrintStartPhase::QGL);

    PreprintEntry e;
    e.total_seconds = 400;
    e.timestamp = 1000;
    e.phase_durations = {{homing, 30}, {mesh, 120}};
    PreprintPredictor predictor;
    predictor.add_entry(e);

    SECTION("heating is already inside the recorded total") {
        const auto est = predictor.estimate({100.0f, 70.0f, true});
        CHECK(est.total_seconds == 400.0f);
        CHECK(est.durations.at(mesh) == 120.0f);
        CHECK(est.durations.at(static_cast<int>(PrintStartPhase::HEATING_BED)) == 70.0f);
    }

    SECTION("a skipped phase comes off the wall clock") {
        CHECK(predictor.estimate({}, {{mesh, false}}).total_seconds == 280.0f);
    }

    SECTION("a phase the start runs adds nothing: the wall clock already holds it") {
        // AD5M-style: the matcher never named the mesh, the option is on.
        const auto est = predictor.estimate({}, {{qgl, true}, {mesh, true}});
        CHECK(est.durations.count(qgl) == 0);
        CHECK(est.total_seconds == 400.0f);
    }
}

TEST_CASE("PreprintPredictor: a skipped phase takes off its share of the whole window",
          "[print][predictor][estimate]") {
    PreprintConfigScope config;
    const int mesh = static_cast<int>(PrintStartPhase::BED_MESH);

    // The older start meshed for 120s, the newer one did not; each spent 280s
    // on everything else, so skipping the mesh leaves 280s whatever the weights.
    PreprintEntry meshed;
    meshed.total_seconds = 400;
    meshed.timestamp = 1000;
    meshed.phase_durations = {{mesh, 120}};
    PreprintEntry unmeshed;
    unmeshed.total_seconds = 280;
    unmeshed.timestamp = 2000;
    PreprintPredictor predictor;
    predictor.add_entry(meshed);
    predictor.add_entry(unmeshed);

    REQUIRE(predictor.predicted_total() > 280);
    CHECK(predictor.estimate({}, {{mesh, false}}).total_seconds ==
          Catch::Approx(280.0f).margin(0.5f));
}

TEST_CASE("PreprintPredictor: a bed at 40C starts warm, below it cold", "[print][predictor]") {
    CHECK(PreprintPredictor::temp_bucket_for(25.0f) == 1);
    CHECK(PreprintPredictor::temp_bucket_for(39.9f) == 1);
    CHECK(PreprintPredictor::temp_bucket_for(40.0f) == 2);
    CHECK(PreprintPredictor::temp_bucket_for(60.0f) == 2);
}

TEST_CASE("PreprintPredictor: a phase turned on that no start in the window ran adds its default",
          "[print][predictor][estimate]") {
    PreprintConfigScope config;
    const int mesh = static_cast<int>(PrintStartPhase::BED_MESH);
    const float mesh_s = static_cast<float>(PreprintPredictor::generic_phase_durations().at(mesh));
    const auto entry = [](int64_t timestamp, std::optional<std::set<int>> skipped) {
        PreprintEntry e;
        e.total_seconds = 300;
        e.timestamp = timestamp;
        e.skipped_phases = std::move(skipped);
        return e;
    };
    PreprintPredictor predictor;

    SECTION("every start skipped it: the default joins the wall clock") {
        predictor.add_entry(entry(1000, std::set<int>{mesh}));
        predictor.add_entry(entry(2000, std::set<int>{mesh}));
        const auto est = predictor.estimate({}, {{mesh, true}});
        CHECK(est.durations.at(mesh) == mesh_s);
        CHECK(est.total_seconds == 300.0f + mesh_s);
    }

    SECTION("the matcher missed it: a start that did not skip it adds nothing") {
        predictor.add_entry(entry(1000, std::set<int>{}));
        CHECK(predictor.estimate({}, {{mesh, true}}).total_seconds == 300.0f);
    }

    SECTION("one start in the window ran it: nothing is added") {
        predictor.add_entry(entry(1000, std::set<int>{mesh}));
        predictor.add_entry(entry(2000, std::set<int>{}));
        CHECK(predictor.estimate({}, {{mesh, true}}).total_seconds == 300.0f);
    }

    SECTION("an entry that recorded nothing is unknown: nothing is added") {
        predictor.add_entry(entry(1000, std::set<int>{mesh}));
        predictor.add_entry(entry(2000, std::nullopt));
        CHECK(predictor.estimate({}, {{mesh, true}}).total_seconds == 300.0f);
    }
}

TEST_CASE("PreprintPredictor: skipped phases round-trip through the stored history",
          "[print][predictor]") {
    PreprintConfigScope config;
    const int mesh = static_cast<int>(PrintStartPhase::BED_MESH);
    const int qgl = static_cast<int>(PrintStartPhase::QGL);
    for (auto skipped :
         {std::optional<std::set<int>>{std::set<int>{mesh, qgl}},
          std::optional<std::set<int>>{std::set<int>{}}, std::optional<std::set<int>>{}}) {
        PreprintEntry e;
        e.total_seconds = 300;
        e.timestamp = 1000;
        e.skipped_phases = skipped;
        PreprintPredictor::append_to_config(e);
    }

    const auto loaded = PreprintPredictor::load_entries_from_config();
    REQUIRE(loaded.size() == 3);
    REQUIRE(loaded[0].skipped_phases.has_value());
    CHECK(*loaded[0].skipped_phases == std::set<int>{mesh, qgl});
    REQUIRE(loaded[1].skipped_phases.has_value());
    CHECK(loaded[1].skipped_phases->empty());
    CHECK_FALSE(loaded[2].skipped_phases.has_value());
}

// ============================================================================
// Heating-aware totals
// ============================================================================
TEST_CASE(
    "PreprintPredictor: an entry that recorded its heating predicts residual plus live heating",
    "[print][predictor]") {
    PreprintPredictor predictor;
    PreprintEntry e;
    e.total_seconds = 894;
    e.timestamp = 2000;
    e.heating_seconds = 470;
    predictor.add_entry(e);

    SECTION("a start with the same heating replays the recorded total") {
        PreprintHeating h;
        h.nozzle_seconds = 200;
        h.bed_seconds = 270;
        h.nozzle_target_known = true;
        CHECK(predictor.estimate(h, {}).total_seconds == 894.0f);
    }
    SECTION("a cheaper start pays only its own heating") {
        PreprintHeating h;
        h.nozzle_seconds = 50;
        h.bed_seconds = 200;
        h.nozzle_target_known = true;
        CHECK(predictor.estimate(h, {}).total_seconds == 674.0f);
    }
    SECTION("a costlier start never sits below its own heating") {
        PreprintHeating h;
        h.nozzle_seconds = 300;
        h.bed_seconds = 300;
        h.nozzle_target_known = true;
        CHECK(predictor.estimate(h, {}).total_seconds == 1024.0f);
    }
}

TEST_CASE("PreprintPredictor: a start that cannot price its heating pays the learned heating",
          "[print][predictor]") {
    PreprintPredictor predictor;
    PreprintEntry e;
    e.total_seconds = 894;
    e.timestamp = 2000;
    e.heating_seconds = 470;
    predictor.add_entry(e);

    SECTION("no targets known: remainder plus learned heating") {
        PreprintHeating h;
        CHECK(predictor.estimate(h, {}).total_seconds == 894.0f);
    }
    SECTION("a bed priced above the learned heating still pays its own") {
        PreprintHeating h;
        h.bed_seconds = 600;
        // 600 s of bed plus the unknown nozzle's 60 s placeholder.
        CHECK(predictor.estimate(h, {}).total_seconds == 1084.0f);
    }
    SECTION("targets known and reached price heating at 0") {
        PreprintHeating h;
        h.nozzle_target_known = true;
        CHECK(predictor.estimate(h, {}).total_seconds == 424.0f);
    }
}

TEST_CASE("PreprintPredictor: legacy history is floored by this start's own heating",
          "[print][predictor]") {
    PreprintPredictor predictor;
    PreprintEntry e;
    e.total_seconds = 405;
    e.timestamp = 1000;
    predictor.add_entry(e);

    SECTION("heating above the recorded total raises the estimate") {
        PreprintHeating h;
        h.nozzle_seconds = 58;
        h.bed_seconds = 412;
        h.nozzle_target_known = true;
        CHECK(predictor.estimate(h, {}).total_seconds == 470.0f);
    }
    SECTION("cheap heating keeps the learned total") {
        PreprintHeating h;
        h.nozzle_seconds = 30;
        h.bed_seconds = 100;
        h.nozzle_target_known = true;
        CHECK(predictor.estimate(h, {}).total_seconds == 405.0f);
    }
}

TEST_CASE("PreprintPredictor: mixed history splits only what recorded heating",
          "[print][predictor]") {
    PreprintPredictor predictor;
    PreprintEntry legacy;
    legacy.total_seconds = 405;
    legacy.timestamp = 1000;
    PreprintEntry split;
    split.total_seconds = 894;
    split.timestamp = 2000;
    split.heating_seconds = 470;
    predictor.add_entry(legacy);
    predictor.add_entry(split);

    PreprintHeating h;
    h.nozzle_seconds = 50;
    h.bed_seconds = 200;
    h.nozzle_target_known = true;
    CHECK(predictor.estimate(h, {}).total_seconds == 674.0f);
}

TEST_CASE("PreprintPredictor: recorded heating round-trips through the stored history",
          "[print][predictor]") {
    PreprintConfigScope config;
    PreprintEntry e;
    e.total_seconds = 894;
    e.timestamp = 1000;
    e.heating_seconds = 470;
    PreprintPredictor::append_to_config(e);
    PreprintEntry none;
    none.total_seconds = 300;
    none.timestamp = 1001;
    PreprintPredictor::append_to_config(none);

    const auto loaded = PreprintPredictor::load_entries_from_config();
    REQUIRE(loaded.size() == 2);
    REQUIRE(loaded[0].heating_seconds.has_value());
    CHECK(*loaded[0].heating_seconds == 470);
    CHECK_FALSE(loaded[1].heating_seconds.has_value());
}
