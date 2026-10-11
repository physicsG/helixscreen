// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "klipper_error_table.h"

#include "catch_amalgamated.hpp"

using helix::printer::klipper_error_lookup;
using helix::printer::KlipperErrorEntry;

// This file is deliberately NOT guarded by HELIX_HAS_CFS. The codes below
// arrive on Klipper's gcode-error channel, which every printer has, and the
// consumer is the application-layer error router. A build with no filament
// hardware compiled in still has to translate them.

TEST_CASE("Klipper-layer codes resolve without any filament backend", "[klipper][errors]") {
    // Faults a machine reports with no CFS box present at all.
    for (const char* code : {"key111", "key298", "key585"}) {
        CAPTURE(code);
        const KlipperErrorEntry* entry = klipper_error_lookup(code);
        REQUIRE(entry != nullptr);
        CHECK(entry->message != nullptr);
        CHECK(std::string(entry->message).length() > 0);
        // Klipper-layer faults are machine-wide, not addressed to a bay.
        CHECK(entry->level == helix::AmsAlertLevel::SYSTEM);
    }
}

TEST_CASE("the pre-heat code keeps its guidance", "[klipper][errors]") {
    const KlipperErrorEntry* entry = klipper_error_lookup("key111");
    REQUIRE(entry != nullptr);
    CHECK(std::string(entry->message) == "Pre-heat the extruder first");
    CHECK(std::string(entry->hint).find("minimum extrude temperature") != std::string::npos);
}

TEST_CASE("CFS hardware codes still resolve from the same table", "[klipper][errors]") {
    // The table holds both: moving it out of the CFS backend must not drop the
    // 37 Creality hardware codes that make up most of it.
    const KlipperErrorEntry* entry = klipper_error_lookup("key849");
    REQUIRE(entry != nullptr);
    CHECK(entry->level == helix::AmsAlertLevel::SLOT);
    REQUIRE(entry->format_values != nullptr);
    // `!! {"code":"key849","values":[1,"B"]}` -> " in unit 1 slot B"
    nlohmann::json values = nlohmann::json::array({1, "B"});
    CHECK(entry->format_values(values) == " in unit 1 slot B");
}

TEST_CASE("an unknown code resolves to nothing", "[klipper][errors]") {
    CHECK(klipper_error_lookup("key000") == nullptr);
    CHECK(klipper_error_lookup("") == nullptr);
    CHECK(klipper_error_lookup("not-a-code") == nullptr);
}
