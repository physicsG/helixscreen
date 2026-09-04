// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_filament_widget_tap_policy.cpp
 * @brief Tests for the Filament Sensor home widget's tap routing and source selection.
 *
 * Run with: ./build/bin/helix-tests "[filament][widget_tap]"
 *
 * The widget itself needs LVGL and a live PrinterState, so every branch that
 * decides *where a tap goes* is lifted into pure functions and tested here.
 * Sensor state: -1 none / 0 empty / 1 loaded / 2 disabled.
 * Print state:   0 standby / 1 printing / 2 paused / 3 complete / 4 cancelled / 5 error.
 */

#include "filament_widget_tap_policy.h"

#include <string>

#include "../catch_amalgamated.hpp"

using helix::ui::decide_tap_destination;
using helix::ui::FilamentTapDestination;
using helix::ui::FilamentTileSource;
using helix::ui::parse_tile_source;
using helix::ui::tile_source_subject;
using helix::ui::tile_source_to_string;

TEST_CASE("Disabled sensor routes to settings in every print state", "[filament][widget_tap]") {
    for (int print_state : {0, 1, 2, 3, 4, 5}) {
        INFO("print_state=" << print_state);
        REQUIRE(decide_tap_destination(2, print_state) == FilamentTapDestination::SensorSettings);
    }
}

TEST_CASE("Printing suppresses the manual action row", "[filament][widget_tap]") {
    REQUIRE(decide_tap_destination(0, 1) == FilamentTapDestination::ModalStatusOnly);
    REQUIRE(decide_tap_destination(1, 1) == FilamentTapDestination::ModalStatusOnly);
}

TEST_CASE("Every non-printing state gets the full modal", "[filament][widget_tap]") {
    // 3 (complete) and 4 (cancelled) are the states a finished print sits in
    // until the user clears it — the printer is idle, the tile is tappable, and
    // load/unload is exactly what someone does next. They were untested while
    // the loop covered only 0/2/5, so a `print_state != 0` guard anywhere in
    // decide_tap_destination() would have gone unnoticed.
    for (int print_state : {0, 2, 3, 4, 5}) {
        INFO("print_state=" << print_state);
        REQUIRE(decide_tap_destination(0, print_state) == FilamentTapDestination::ModalFull);
        REQUIRE(decide_tap_destination(1, print_state) == FilamentTapDestination::ModalFull);
    }
}

TEST_CASE("No configured sensor is unreachable", "[filament][widget_tap]") {
    // The tile is hidden by its filament_sensor_count hardware gate at -1, so a
    // tap cannot land. Assert the policy still refuses rather than falling
    // through to a modal, in case the gate ever regresses.
    for (int print_state : {0, 1, 2, 3, 4, 5}) {
        INFO("print_state=" << print_state);
        REQUIRE(decide_tap_destination(-1, print_state) == FilamentTapDestination::None);
    }
}

TEST_CASE("Disabled beats printing", "[filament][widget_tap]") {
    // Order matters: a disabled sensor mid-print is still a config problem, and
    // the status-only modal cannot fix it.
    REQUIRE(decide_tap_destination(2, 1) == FilamentTapDestination::SensorSettings);
}

TEST_CASE("Tile source parses from saved config", "[filament][widget_tap]") {
    REQUIRE(parse_tile_source("auto") == FilamentTileSource::Auto);
    REQUIRE(parse_tile_source("runout") == FilamentTileSource::Runout);
    REQUIRE(parse_tile_source("toolhead") == FilamentTileSource::Toolhead);
    REQUIRE(parse_tile_source("entry") == FilamentTileSource::Entry);
}

TEST_CASE("Unknown or empty source falls back to Auto", "[filament][widget_tap]") {
    REQUIRE(parse_tile_source("") == FilamentTileSource::Auto);
    REQUIRE(parse_tile_source("nonsense") == FilamentTileSource::Auto);
    REQUIRE(parse_tile_source("RUNOUT") == FilamentTileSource::Auto); // case-sensitive by design
}

TEST_CASE("Source string round-trips", "[filament][widget_tap]") {
    for (auto source : {FilamentTileSource::Auto, FilamentTileSource::Runout,
                        FilamentTileSource::Toolhead, FilamentTileSource::Entry}) {
        REQUIRE(parse_tile_source(tile_source_to_string(source)) == source);
    }
}

TEST_CASE("Each source names an existing subject", "[filament][widget_tap]") {
    REQUIRE(std::string(tile_source_subject(FilamentTileSource::Auto)) ==
            "filament_runout_detected");
    REQUIRE(std::string(tile_source_subject(FilamentTileSource::Runout)) ==
            "filament_runout_detected");
    REQUIRE(std::string(tile_source_subject(FilamentTileSource::Toolhead)) ==
            "filament_toolhead_detected");
    REQUIRE(std::string(tile_source_subject(FilamentTileSource::Entry)) ==
            "filament_entry_detected");
}
