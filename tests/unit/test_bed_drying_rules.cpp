// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_bed_drying_rules.cpp
 * @brief The pure decisions behind drying on the heated bed
 *        (prestonbrown/helixscreen#1730).
 */

#include "../../include/bed_drying.h"

#include "../catch_amalgamated.hpp"

using namespace helix::bed_drying;

TEST_CASE("enclosure: the override wins, AUTO needs the DB flag or a chamber heater",
          "[bed_drying]") {
    CHECK(is_enclosed(EnclosureStyle::AUTO, true, false));
    CHECK(is_enclosed(EnclosureStyle::AUTO, false, true));
    CHECK_FALSE(is_enclosed(EnclosureStyle::AUTO, false, false));
    CHECK(is_enclosed(EnclosureStyle::ENCLOSED, false, false));
    CHECK_FALSE(is_enclosed(EnclosureStyle::OPEN, true, true));
}

TEST_CASE("availability needs a heated bed, an enclosure and 130 mm of Z", "[bed_drying]") {
    CHECK(available(true, true, true, 0.0, 250.0));
    CHECK(available(true, true, true, 0.0, 130.0));
    CHECK_FALSE(available(true, true, true, 0.0, 129.0));
    CHECK_FALSE(available(true, true, true, 20.0, 140.0));
    CHECK_FALSE(available(false, true, true, 0.0, 250.0));
    CHECK_FALSE(available(true, false, true, 0.0, 250.0));
    CHECK_FALSE(available(true, true, false, 0.0, 250.0));
}

TEST_CASE("the clearance move stops 20 mm short of the end of Z travel", "[bed_drying]") {
    CHECK(clearance_z(250.0) == Catch::Approx(230.0));
    CHECK(clearance_z(130.0) == Catch::Approx(110.0));
}

TEST_CASE("bed temperature: the material's table value, capped at 90 C and the bed max",
          "[bed_drying]") {
    CHECK(bed_temp_c(kMaterials[0], 120) == 70); // PLA
    CHECK(bed_temp_c(kMaterials[2], 120) == 85); // PETG
    CHECK(bed_temp_c(kMaterials[4], 120) == 90); // ABS/ASA/PC/PA: 100 capped at 90
    CHECK(bed_temp_c(kMaterials[4], 80) == 80);  // a bed that tops out lower
    CHECK(bed_temp_c(kMaterials[0], 0) == 70);   // unknown bed max: no extra cap
    for (const auto& m : kMaterials) {
        CHECK(m.hours == 12);
    }
}

TEST_CASE("the chamber: a dryer wins, else a plain heater, else nothing", "[bed_drying]") {
    CHECK(chamber_assist(true, true) == ChamberAssist::Dryer);
    CHECK(chamber_assist(true, false) == ChamberAssist::Dryer);
    CHECK(chamber_assist(false, true) == ChamberAssist::Heater);
    CHECK(chamber_assist(false, false) == ChamberAssist::None);
}

TEST_CASE("chamber temperature: the material's air value, capped at the heater max",
          "[bed_drying]") {
    CHECK(chamber_temp_c(kMaterials[0], 80) == 50); // PLA
    CHECK(chamber_temp_c(kMaterials[4], 80) == 80); // ABS/ASA/PC/PA
    CHECK(chamber_temp_c(kMaterials[4], 60) == 60); // a heater that tops out lower
    CHECK(chamber_temp_c(kMaterials[2], 0) == 65);  // unknown max: no cap
}

TEST_CASE("the unload is offered unless a sensor says the toolhead is empty", "[bed_drying]") {
    CHECK(unload_offer(false) == UnloadOffer::None);
    CHECK(unload_offer(true) == UnloadOffer::Recommended);
    CHECK(unload_offer(std::nullopt) == UnloadOffer::Offered);
}

TEST_CASE("a filament-system unload is done only once it has been busy and gone idle",
          "[bed_drying]") {
    CHECK(unload_progress(false, false, false) == UnloadProgress::Waiting);
    CHECK(unload_progress(false, true, false) == UnloadProgress::Waiting);
    CHECK(unload_progress(true, true, false) == UnloadProgress::Waiting);
    CHECK(unload_progress(true, false, false) == UnloadProgress::Done);
    CHECK(unload_progress(false, false, true) == UnloadProgress::Failed);
    CHECK(unload_progress(true, false, true) == UnloadProgress::Failed);
}

TEST_CASE("toolhead loaded: only a toolhead sensor can say empty", "[bed_drying]") {
    CHECK(toolhead_loaded_from(false, true, true) == std::optional<bool>(false));
    CHECK(toolhead_loaded_from(true, std::nullopt, false) == std::optional<bool>(true));
    CHECK(toolhead_loaded_from(std::nullopt, std::nullopt, true) == std::optional<bool>(true));
    CHECK(toolhead_loaded_from(std::nullopt, true, false) == std::optional<bool>(true));
    CHECK_FALSE(toolhead_loaded_from(std::nullopt, false, false).has_value());
    CHECK_FALSE(toolhead_loaded_from(std::nullopt, std::nullopt, false).has_value());
}

TEST_CASE("run timing: the flip is due at the midpoint and the run ends at its planned end",
          "[bed_drying]") {
    const long long start = 1000;
    const long long end = start + 12 * 3600;
    CHECK_FALSE(flip_due(start + 6 * 3600 - 1, start, end));
    CHECK(flip_due(start + 6 * 3600, start, end));
    CHECK(phase_at(end - 1, end) == Phase::Running);
    CHECK(phase_at(end, end) == Phase::Ended);
}

TEST_CASE("the remove prompt waits for the bed to read below 40 C", "[bed_drying]") {
    CHECK_FALSE(may_prompt_removal(70.0));
    CHECK_FALSE(may_prompt_removal(40.0));
    CHECK(may_prompt_removal(39.9));
}
