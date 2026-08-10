// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_multiace_vs_anycubic_detection.cpp
 * @brief Telling multiACE's `ace` object apart from Anycubic's.
 *
 * Two unrelated stacks register a Klipper object named exactly `ace`:
 *
 *   - Anycubic community drivers (ValgACE / BunnyACE / DuckACE), a real ACE hub
 *     whose status carries a top-level `slots[]`.
 *   - decay71/multiACE, 1-4 ACE units bolted onto a Snapmaker U1, whose status
 *     carries `aces[].slots[]` and no top-level `slots[]` at all.
 *
 * Detection runs off printer.objects.list, where only NAMES are available, so
 * the shape cannot be consulted. Claiming multiACE as Anycubic pointed
 * AmsBackendAce at a payload it cannot read; it then fell through to a
 * /server/ace/* REST bridge multiACE does not serve, leaving an empty
 * multi-filament panel and a "bridge not found" warning — strictly worse than
 * the U1's own native backend, which reads the same four heads correctly.
 *
 * Two name-only signals separate them, either sufficient:
 *   - `ace_bg_swap` / `ace_tipform`, which only multiACE registers
 *   - `filament_detect`, the Snapmaker U1 firmware signature, which no Anycubic
 *     printer has
 */

#include "ams_types.h"
#include "printer_discovery.h"

#include "../catch_amalgamated.hpp"

#include <string>
#include <vector>

using namespace helix;

namespace {

nlohmann::json object_list(const std::vector<std::string>& names) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& n : names) {
        arr.push_back(n);
    }
    return arr;
}

/// The Snapmaker U1 firmware objects that always accompany multiACE.
const std::vector<std::string> kU1Base = {"extruder",         "extruder1",
                                          "extruder2",        "extruder3",
                                          "filament_detect",  "filament_feed left",
                                          "filament_feed right", "print_task_config",
                                          "toolhead",         "gcode"};

std::vector<std::string> with(std::vector<std::string> base,
                              const std::vector<std::string>& extra) {
    base.insert(base.end(), extra.begin(), extra.end());
    return base;
}

} // namespace

TEST_CASE("multiACE's `ace` object is not mistaken for an Anycubic ACE",
          "[ams][detection][multiace][snapmaker]") {

    SECTION("U1 + multiACE: both markers present") {
        PrinterDiscovery d;
        d.parse_objects(object_list(with(kU1Base, {"ace", "ace_bg_swap", "ace_tipform"})));

        // Not Anycubic. Until AmsBackendMultiAce lands, the U1's own backend is
        // the right answer — it reads the same four heads from print_task_config.
        CHECK(d.mmu_type() != AmsType::ACE);
        CHECK(d.mmu_type() == AmsType::SNAPMAKER);
    }

    SECTION("U1 + multiACE with the optional extras removed") {
        // A user can delete [ace_bg_swap]/[ace_tipform] from ace.cfg. The U1
        // firmware signature alone still has to be enough, because the failure
        // mode it prevents is total.
        PrinterDiscovery d;
        d.parse_objects(object_list(with(kU1Base, {"ace"})));

        CHECK(d.mmu_type() != AmsType::ACE);
        CHECK(d.mmu_type() == AmsType::SNAPMAKER);
    }

    SECTION("a genuine Anycubic community ACE is still detected") {
        // No U1 firmware objects, no multiACE markers — this is the stack the
        // `ace` name originally meant, and it must keep working.
        PrinterDiscovery d;
        d.parse_objects(object_list({"extruder", "heater_bed", "toolhead", "gcode", "ace"}));

        CHECK(d.mmu_type() == AmsType::ACE);
    }

    SECTION("native Anycubic `filament_hub` is unambiguous and unaffected") {
        PrinterDiscovery d;
        d.parse_objects(object_list({"extruder", "heater_bed", "toolhead", "filament_hub"}));

        CHECK(d.mmu_type() == AmsType::ACE);
    }

    SECTION("Kobra S1 fork `ace_instance_N` is unambiguous and unaffected") {
        PrinterDiscovery d;
        d.parse_objects(
            object_list({"extruder", "heater_bed", "toolhead", "ace_instance_0", "ace_instance_1"}));

        CHECK(d.mmu_type() == AmsType::ACE);
        CHECK(d.ace_object_names().size() == 2);
    }

    SECTION("a real MMU still outranks the bare `ace` object") {
        // Happy Hare on a machine that also exposes `ace` keeps winning — the
        // deferred bare-`ace` decision must not steal the type from an MMU that
        // already claimed it.
        PrinterDiscovery d;
        d.parse_objects(object_list({"extruder", "heater_bed", "toolhead", "mmu", "ace"}));

        CHECK(d.mmu_type() == AmsType::HAPPY_HARE);
    }
}
