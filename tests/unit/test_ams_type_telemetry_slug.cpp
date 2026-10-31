// SPDX-License-Identifier: GPL-3.0-or-later

// ams.type in the telemetry hardware profile is a published schema the
// dashboard groups by: an existing value must never change.

#include "ams_types.h"

#include <string>

#include "../catch_amalgamated.hpp"

using helix::ams_type_telemetry_slug;
using helix::AmsType;

TEST_CASE("Telemetry ams.type slugs are stable", "[telemetry][ams]") {
    CHECK(std::string(ams_type_telemetry_slug(AmsType::NONE)) == "unknown");
    CHECK(std::string(ams_type_telemetry_slug(AmsType::HAPPY_HARE)) == "happy_hare");
    CHECK(std::string(ams_type_telemetry_slug(AmsType::AFC)) == "afc");
    CHECK(std::string(ams_type_telemetry_slug(AmsType::ACE)) == "ace");
    CHECK(std::string(ams_type_telemetry_slug(AmsType::TOOL_CHANGER)) == "tool_changer");
    CHECK(std::string(ams_type_telemetry_slug(AmsType::AD5X_IFS)) == "ifs");
    CHECK(std::string(ams_type_telemetry_slug(AmsType::CFS)) == "cfs");
    CHECK(std::string(ams_type_telemetry_slug(AmsType::QIDI_BOX)) == "qidi_box");
    CHECK(std::string(ams_type_telemetry_slug(AmsType::OPENAMS)) == "openams");
    CHECK(std::string(ams_type_telemetry_slug(AmsType::SNAPMAKER)) == "snapmaker");
    CHECK(std::string(ams_type_telemetry_slug(AmsType::INDX)) == "indx");
}

// hardware/expected entries are persisted in users' settings: an existing
// value must never change, and every one must map back to its own type.
TEST_CASE("Expected-hardware names are stable and round-trip", "[ams][hardware][validator]") {
    using helix::ams_type_for_expected_hardware;
    using helix::ams_type_traits;
    CHECK(std::string(ams_type_traits(AmsType::HAPPY_HARE).expected_hardware) == "mmu");
    CHECK(std::string(ams_type_traits(AmsType::AFC).expected_hardware) == "AFC");
    CHECK(std::string(ams_type_traits(AmsType::ACE).expected_hardware) == "ace");
    CHECK(std::string(ams_type_traits(AmsType::TOOL_CHANGER).expected_hardware) == "toolchanger");
    CHECK(std::string(ams_type_traits(AmsType::INDX).expected_hardware) == "indx");

    for (const auto& row : helix::kAmsTypeTraits) {
        CAPTURE(row.name);
        if (row.type == AmsType::NONE) {
            CHECK(std::string(row.expected_hardware).empty());
            continue;
        }
        CHECK(ams_type_for_expected_hardware(row.expected_hardware) == row.type);
    }
    CHECK(ams_type_for_expected_hardware("") == AmsType::NONE);
    CHECK(ams_type_for_expected_hardware("afc") == AmsType::NONE); // case matters: "AFC"
    CHECK(ams_type_for_expected_hardware("heater_bed") == AmsType::NONE);
}

TEST_CASE("Every AmsType has exactly one traits row", "[ams]") {
    for (int v = 0; v <= static_cast<int>(AmsType::INDX); ++v) {
        const auto type = static_cast<AmsType>(v);
        int rows = 0;
        for (const auto& row : helix::kAmsTypeTraits) {
            rows += row.type == type ? 1 : 0;
        }
        CAPTURE(v);
        CHECK(rows == 1);
        CHECK(helix::ams_type_traits(type).type == type);
    }
}
