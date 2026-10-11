// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// The bed temperature an in-app mesh calibration asks the printer to probe at.
// Inputs are decidegrees, the unit PrinterState's bed subjects hold, so each case
// feeds exactly what the panel reads.

#include "bed_mesh_probe_temp.h"

#include "../catch_amalgamated.hpp"

using helix::bed_mesh::DEFAULT_PROBE_BED_TEMP_C;
using helix::bed_mesh::probe_bed_temp_c;

TEST_CASE("probe bed temp: a set target wins", "[bed_mesh][probe_temp]") {
    CHECK(probe_bed_temp_c(/*target_deci=*/1050, /*current_deci=*/240) == 105);
    // Below the default is honoured, not raised to it.
    CHECK(probe_bed_temp_c(450, 200) == 45);
    // Whole degrees, never above what was asked.
    CHECK(probe_bed_temp_c(1059, 240) == 105);
}

TEST_CASE("probe bed temp: a target below a hot bed does not make it cool",
          "[bed_mesh][probe_temp]") {
    // The heat-and-wait also waits for cooling, so a bed above the default is
    // probed at the temperature it already has whatever lower target is set.
    CHECK(probe_bed_temp_c(/*target_deci=*/600, /*current_deci=*/1000) == 100);
    CHECK(probe_bed_temp_c(700, 950) == 95);
    // "Above 60" is the reading itself, not its floor: 60.5 with a 45 target
    // asks for 60, which the bed already satisfies.
    CHECK(probe_bed_temp_c(450, 605) == 60);
    // A hotter target still wins.
    CHECK(probe_bed_temp_c(1050, 950) == 105);
}

TEST_CASE("probe bed temp: an idle bed still hot is not left to cool", "[bed_mesh][probe_temp]") {
    // Heater off, bed warm from a print: probe at the temperature it already has,
    // floored so a heat-and-wait is satisfied at once instead of waiting for the
    // bed to shed the fraction.
    CHECK(probe_bed_temp_c(0, 952) == 95);
    CHECK(probe_bed_temp_c(0, 611) == 61);
    CHECK(probe_bed_temp_c(0, 619) == 61);
}

TEST_CASE("probe bed temp: a cool bed with no target probes at 60", "[bed_mesh][probe_temp]") {
    CHECK(probe_bed_temp_c(0, 240) == 60);
    CHECK(DEFAULT_PROBE_BED_TEMP_C == 60);
    // "Above 60" is strict, and 60.9 floors to 60.
    CHECK(probe_bed_temp_c(0, 600) == 60);
    CHECK(probe_bed_temp_c(0, 609) == 60);
    CHECK(probe_bed_temp_c(0, 0) == 60);
}

TEST_CASE("probe bed temp: readings that set nothing fall back to 60", "[bed_mesh][probe_temp]") {
    // A negative target is not a set one, and neither is a fraction of a degree.
    CHECK(probe_bed_temp_c(-10, 240) == 60);
    CHECK(probe_bed_temp_c(5, 240) == 60);
    // A disconnected thermistor can read far below zero.
    CHECK(probe_bed_temp_c(0, -2730) == 60);
}
