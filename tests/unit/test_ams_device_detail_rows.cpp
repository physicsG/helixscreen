// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ams_device_detail_rows.cpp
 * @brief The device operations overlay's read-only facts: one row per value a
 *        backend publishes, none for a value it does not.
 */

#include "ui_ams_device_operations_overlay.h"

#include "ams_types.h"

#include <string>

#include "../catch_amalgamated.hpp"

using namespace helix;
using helix::ui::ams_device_detail_rows;
using helix::ui::DeviceDetailRow;

namespace {

const DeviceDetailRow* find_row(const std::vector<DeviceDetailRow>& rows,
                                const std::string& label) {
    for (const auto& r : rows) {
        if (r.label == label) {
            return &r;
        }
    }
    return nullptr;
}

AmsUnit named_unit(int index, const std::string& name) {
    AmsUnit unit;
    unit.unit_index = index;
    unit.name = name;
    unit.display_name = name;
    return unit;
}

} // namespace

TEST_CASE("device details: nothing published, no rows", "[ams][device_ops][details]") {
    AmsSystemInfo info;
    info.units.push_back(named_unit(0, "Box"));
    CHECK(ams_device_detail_rows(info).empty());
}

TEST_CASE("device details: a unit's firmware and serial", "[ams][device_ops][details]") {
    AmsSystemInfo info;
    AmsUnit unit = named_unit(0, "CFS 1");
    unit.firmware_version = "1.1.3";
    unit.serial_number = "SN123";
    info.units.push_back(unit);
    AmsUnit bare = named_unit(1, "CFS 2");
    bare.firmware_version = "1.1.2";
    info.units.push_back(bare);

    const auto rows = ams_device_detail_rows(info);
    REQUIRE(rows.size() == 3);
    const auto* fw = find_row(rows, "CFS 1 firmware");
    REQUIRE(fw != nullptr);
    CHECK(fw->value == "1.1.3");
    const auto* sn = find_row(rows, "CFS 1 serial");
    REQUIRE(sn != nullptr);
    CHECK(sn->value == "SN123");
    CHECK(find_row(rows, "CFS 2 serial") == nullptr);
}

TEST_CASE("device details: an absent unit says nothing", "[ams][device_ops][details]") {
    AmsSystemInfo info;
    AmsUnit gap = named_unit(0, "CFS 1");
    gap.absent = true;
    gap.firmware_version = "stale";
    info.units.push_back(gap);
    CHECK(ams_device_detail_rows(info).empty());
}

TEST_CASE("device details: purge volume, Spoolman mode and pending spool",
          "[ams][device_ops][details]") {
    AmsSystemInfo info;
    info.toolchange_purge_volume = 120.0f;
    info.spoolman_mode = SpoolmanMode::PULL;
    info.pending_spool_id = 12;

    const auto rows = ams_device_detail_rows(info);
    REQUIRE(rows.size() == 3);
    const auto* purge = find_row(rows, "Toolchange purge");
    REQUIRE(purge != nullptr);
    CHECK(purge->value == "120 mm\xC2\xB3");
    const auto* mode = find_row(rows, "Spoolman");
    REQUIRE(mode != nullptr);
    CHECK(mode->value == "Pull");
    const auto* pending = find_row(rows, "Pending spool");
    REQUIRE(pending != nullptr);
    CHECK(pending->value == "#12");

    SECTION("unset values add no rows") {
        info.toolchange_purge_volume = 0.0f;
        info.spoolman_mode = SpoolmanMode::OFF;
        info.pending_spool_id = -1;
        CHECK(ams_device_detail_rows(info).empty());
    }
}
