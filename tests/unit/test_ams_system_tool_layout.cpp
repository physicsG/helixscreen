// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_mock.h"
#include "ams_types.h"
#include "ui/ams_drawing_utils.h"

#include <map>
#include <optional>
#include <vector>

#include "../catch_amalgamated.hpp"

/**
 * @file test_ams_system_tool_layout.cpp
 * @brief Unit tests for compute_system_tool_layout()
 *
 * Tests the physical nozzle position calculation that fixes the bug where
 * HUB units with unique per-lane mapped_tool values (real AFC behavior)
 * inflated the total nozzle count in the system path canvas.
 */

using namespace ams_draw;

// ============================================================================
// Core: HUB units with unique per-lane mapped_tools (the user's bug)
// ============================================================================

TEST_CASE("SystemToolLayout: 3 HUB units with unique mapped_tools", "[ams][tool_layout]") {
    // 3 HUB units, slots have mapped_tool {0-3}, {4-7}, {8-11}
    // Each HUB unit should be 1 physical nozzle regardless of mapped_tool spread
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    for (int u = 0; u < 3; ++u) {
        AmsUnit unit;
        unit.unit_index = u;
        unit.slot_count = 4;
        unit.first_slot_global_index = u * 4;
        unit.topology = PathTopology::HUB;
        for (int s = 0; s < 4; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = u * 4 + s;
            slot.mapped_tool = u * 4 + s; // Unique per lane
            unit.slots.push_back(slot);
        }
        info.units.push_back(unit);
    }
    info.total_slots = 12;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 3);
    REQUIRE(layout.units.size() == 3);
    for (int u = 0; u < 3; ++u) {
        CHECK(layout.units[u].tool_count == 1);
        CHECK(layout.units[u].first_physical_tool == u);
    }
}

// ============================================================================
// User's exact mixed setup (Box Turtle + 2x OpenAMS)
// ============================================================================

TEST_CASE("SystemToolLayout: user's exact mixed setup", "[ams][tool_layout]") {
    // HUB(mapped 0-3) + HUB(mapped 4-7) + PARALLEL(mapped 8-11)
    AmsBackendMock backend(4);
    backend.set_mixed_topology_mode(true);

    auto info = backend.get_system_info();

    // Apply real AFC mapped_tool values (unique per lane, even for HUB units)
    for (int i = 0; i < 4; ++i) {
        info.get_slot_global(i)->mapped_tool = i;         // BT: T0-T3
        info.get_slot_global(4 + i)->mapped_tool = 4 + i; // AMS_1: T4-T7
        info.get_slot_global(8 + i)->mapped_tool = 8 + i; // AMS_2: T8-T11
    }

    auto layout = compute_system_tool_layout(info, &backend);

    CHECK(layout.total_physical_tools == 6);
    REQUIRE(layout.units.size() == 3);

    // Unit 0: Box Turtle (PARALLEL) → 4 nozzles
    CHECK(layout.units[0].first_physical_tool == 0);
    CHECK(layout.units[0].tool_count == 4);

    // Unit 1: AMS_1 (HUB) → 1 nozzle
    CHECK(layout.units[1].first_physical_tool == 4);
    CHECK(layout.units[1].tool_count == 1);

    // Unit 2: AMS_2 (HUB) → 1 nozzle
    CHECK(layout.units[2].first_physical_tool == 5);
    CHECK(layout.units[2].tool_count == 1);
}

// ============================================================================
// Mock mixed topology (HUB + HUB + PARALLEL)
// ============================================================================

TEST_CASE("SystemToolLayout: mock mixed topology", "[ams][tool_layout]") {
    AmsBackendMock backend(4);
    backend.set_mixed_topology_mode(true);

    auto info = backend.get_system_info();

    // After mock update, HUB units should have unique per-lane mapped_tool values
    auto layout = compute_system_tool_layout(info, &backend);

    CHECK(layout.total_physical_tools == 6);
    REQUIRE(layout.units.size() == 3);

    // PARALLEL unit: 4 tools
    CHECK(layout.units[0].tool_count == 4);
    // HUB units: 1 tool each
    CHECK(layout.units[1].tool_count == 1);
    CHECK(layout.units[2].tool_count == 1);
}

// ============================================================================
// All-PARALLEL system (tool changer, 3 units)
// ============================================================================

TEST_CASE("SystemToolLayout: all-PARALLEL system", "[ams][tool_layout]") {
    AmsSystemInfo info;
    info.type = AmsType::TOOL_CHANGER;

    for (int u = 0; u < 3; ++u) {
        AmsUnit unit;
        unit.unit_index = u;
        unit.slot_count = 4;
        unit.first_slot_global_index = u * 4;
        unit.topology = PathTopology::PARALLEL;
        for (int s = 0; s < 4; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = u * 4 + s;
            slot.mapped_tool = u * 4 + s;
            unit.slots.push_back(slot);
        }
        info.units.push_back(unit);
    }
    info.total_slots = 12;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 12);
    for (int u = 0; u < 3; ++u) {
        CHECK(layout.units[u].tool_count == 4);
        CHECK(layout.units[u].first_physical_tool == u * 4);
    }
}

// ============================================================================
// Virtual→physical mapping for active tool highlighting
// ============================================================================

TEST_CASE("SystemToolLayout: virtual to physical mapping", "[ams][tool_layout]") {
    // HUB unit with mapped_tool {4,5,6,7} → all map to same physical nozzle
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;
    unit.first_slot_global_index = 0;
    unit.topology = PathTopology::HUB;
    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = 4 + s;
        unit.slots.push_back(slot);
    }
    info.units.push_back(unit);
    info.total_slots = 4;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 1);

    // All virtual tools 4-7 should map to physical nozzle 0
    for (int v = 4; v <= 7; ++v) {
        auto it = layout.virtual_to_physical.find(v);
        REQUIRE(it != layout.virtual_to_physical.end());
        CHECK(it->second == 0);
    }
}

// ============================================================================
// Physical→virtual label mapping
// ============================================================================

TEST_CASE("SystemToolLayout: physical to virtual label mapping", "[ams][tool_layout]") {
    // HUB(mapped 0-3) + HUB(mapped 4-7)
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    for (int u = 0; u < 2; ++u) {
        AmsUnit unit;
        unit.unit_index = u;
        unit.slot_count = 4;
        unit.first_slot_global_index = u * 4;
        unit.topology = PathTopology::HUB;
        for (int s = 0; s < 4; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = u * 4 + s;
            slot.mapped_tool = u * 4 + s;
            unit.slots.push_back(slot);
        }
        info.units.push_back(unit);
    }
    info.total_slots = 8;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 2);
    REQUIRE(layout.physical_to_virtual_label.size() == 2);
    CHECK(layout.physical_to_virtual_label[0] == 0); // Min of {0,1,2,3}
    CHECK(layout.physical_to_virtual_label[1] == 4); // Min of {4,5,6,7}
}

// ============================================================================
// Single HUB unit (no multi-tool)
// ============================================================================

TEST_CASE("SystemToolLayout: single HUB unit", "[ams][tool_layout]") {
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;
    unit.first_slot_global_index = 0;
    unit.topology = PathTopology::HUB;
    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = s;
        unit.slots.push_back(slot);
    }
    info.units.push_back(unit);
    info.total_slots = 4;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 1);
    REQUIRE(layout.units.size() == 1);
    CHECK(layout.units[0].tool_count == 1);
    CHECK(layout.units[0].first_physical_tool == 0);
}

// ============================================================================
// Empty system
// ============================================================================

TEST_CASE("SystemToolLayout: empty system", "[ams][tool_layout]") {
    AmsSystemInfo info;
    info.type = AmsType::NONE;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 0);
    CHECK(layout.units.empty());
    CHECK(layout.virtual_to_physical.empty());
    CHECK(layout.physical_to_virtual_label.empty());
}

// ============================================================================
// PARALLEL unit with no mapped_tool data (fallback)
// ============================================================================

TEST_CASE("SystemToolLayout: PARALLEL with no mapped_tool data", "[ams][tool_layout]") {
    AmsSystemInfo info;
    info.type = AmsType::TOOL_CHANGER;

    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;
    unit.first_slot_global_index = 0;
    unit.topology = PathTopology::PARALLEL;
    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = -1; // No mapping data
        unit.slots.push_back(slot);
    }
    info.units.push_back(unit);
    info.total_slots = 4;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 4);
    CHECK(layout.units[0].tool_count == 4);
}

// ============================================================================
// HUB unit with no mapped_tool data
// ============================================================================

TEST_CASE("SystemToolLayout: HUB with no mapped_tool data", "[ams][tool_layout]") {
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;
    unit.first_slot_global_index = 0;
    unit.topology = PathTopology::HUB;
    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = -1; // No mapping data
        unit.slots.push_back(slot);
    }
    info.units.push_back(unit);
    info.total_slots = 4;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 1);
    CHECK(layout.units[0].tool_count == 1);
}

// ============================================================================
// Full user scenario: virtual→physical for active tool in mixed setup
// ============================================================================

TEST_CASE("SystemToolLayout: mixed setup active tool mapping", "[ams][tool_layout]") {
    AmsBackendMock backend(4);
    backend.set_mixed_topology_mode(true);

    auto info = backend.get_system_info();

    // Apply real AFC mapped_tool values
    for (int i = 0; i < 4; ++i) {
        info.get_slot_global(i)->mapped_tool = i;
        info.get_slot_global(4 + i)->mapped_tool = 4 + i;
        info.get_slot_global(8 + i)->mapped_tool = 8 + i;
    }

    auto layout = compute_system_tool_layout(info, &backend);

    // BT virtual tools 0-3 → physical 0-3 (PARALLEL, each maps to own nozzle)
    for (int v = 0; v < 4; ++v) {
        auto it = layout.virtual_to_physical.find(v);
        REQUIRE(it != layout.virtual_to_physical.end());
        CHECK(it->second == v);
    }

    // AMS_1 virtual tools 4-7 → physical 4 (single HUB nozzle)
    for (int v = 4; v < 8; ++v) {
        auto it = layout.virtual_to_physical.find(v);
        REQUIRE(it != layout.virtual_to_physical.end());
        CHECK(it->second == 4);
    }

    // AMS_2 virtual tools 8-11 → physical 5 (single HUB nozzle)
    for (int v = 8; v < 12; ++v) {
        auto it = layout.virtual_to_physical.find(v);
        REQUIRE(it != layout.virtual_to_physical.end());
        CHECK(it->second == 5);
    }
}

// ============================================================================
// hub_tool_label overrides min_virtual_tool for labels
// ============================================================================

TEST_CASE("SystemToolLayout: hub_tool_label overrides min_virtual_tool for labels",
          "[ams][tool_layout]") {
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;
    unit.first_slot_global_index = 0;
    unit.topology = PathTopology::HUB;
    unit.hub_tool_label = 4;
    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = 4 + s;
        unit.slots.push_back(slot);
    }
    info.units.push_back(unit);
    info.total_slots = 4;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 1);
    REQUIRE(layout.physical_to_virtual_label.size() == 1);
    CHECK(layout.physical_to_virtual_label[0] == 4);
}

// ============================================================================
// hub_tool_label maps to physical nozzle
// ============================================================================

TEST_CASE("SystemToolLayout: hub_tool_label used for display label not virtual mapping",
          "[ams][tool_layout]") {
    // hub_tool_label affects physical_to_virtual_label (display) but NOT virtual_to_physical
    // (to avoid conflicts when hub_tool_label overlaps another unit's virtual tool range)
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;
    unit.first_slot_global_index = 0;
    unit.topology = PathTopology::HUB;
    unit.hub_tool_label = 5;
    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = 8 + s;
        unit.slots.push_back(slot);
    }
    info.units.push_back(unit);
    info.total_slots = 4;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 1);
    // Virtual tools 8-11 map to physical 0
    for (int v = 8; v <= 11; ++v) {
        CHECK(layout.virtual_to_physical[v] == 0);
    }
    // hub_tool_label=5 should NOT be in virtual_to_physical (would conflict with other units)
    CHECK(layout.virtual_to_physical.find(5) == layout.virtual_to_physical.end());
    // But it SHOULD be used for the display label
    REQUIRE(layout.physical_to_virtual_label.size() == 1);
    CHECK(layout.physical_to_virtual_label[0] == 5);
}

// ============================================================================
// Mixed setup correct labels with hub_tool_label
// ============================================================================

TEST_CASE("SystemToolLayout: mixed setup correct labels with hub_tool_label",
          "[ams][tool_layout]") {
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    // Unit 0: PARALLEL with 4 tools
    {
        AmsUnit unit;
        unit.unit_index = 0;
        unit.slot_count = 4;
        unit.first_slot_global_index = 0;
        unit.topology = PathTopology::PARALLEL;
        for (int s = 0; s < 4; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = s;
            slot.mapped_tool = s;
            unit.slots.push_back(slot);
        }
        info.units.push_back(unit);
    }

    // Unit 1: HUB with hub_tool_label=4
    {
        AmsUnit unit;
        unit.unit_index = 1;
        unit.slot_count = 4;
        unit.first_slot_global_index = 4;
        unit.topology = PathTopology::HUB;
        unit.hub_tool_label = 4;
        for (int s = 0; s < 4; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = 4 + s;
            slot.mapped_tool = 4 + s;
            unit.slots.push_back(slot);
        }
        info.units.push_back(unit);
    }

    // Unit 2: HUB with hub_tool_label=5
    {
        AmsUnit unit;
        unit.unit_index = 2;
        unit.slot_count = 4;
        unit.first_slot_global_index = 8;
        unit.topology = PathTopology::HUB;
        unit.hub_tool_label = 5;
        for (int s = 0; s < 4; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = 8 + s;
            slot.mapped_tool = 8 + s;
            unit.slots.push_back(slot);
        }
        info.units.push_back(unit);
    }

    info.total_slots = 12;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 6);
    REQUIRE(layout.physical_to_virtual_label.size() == 6);
    CHECK(layout.physical_to_virtual_label[0] == 0);
    CHECK(layout.physical_to_virtual_label[1] == 1);
    CHECK(layout.physical_to_virtual_label[2] == 2);
    CHECK(layout.physical_to_virtual_label[3] == 3);
    CHECK(layout.physical_to_virtual_label[4] == 4);
    CHECK(layout.physical_to_virtual_label[5] == 5);
}

// ============================================================================
// Shared toolhead: multiple HUB units feeding into one extruder
// ============================================================================

TEST_CASE("SystemToolLayout: 3 HUB units sharing same hub_tool_label merge to 1 nozzle",
          "[ams][tool_layout]") {
    // Simulates 2x BoxTurtle + 1x ViViD, all feeding into a single T0 extruder
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    for (int u = 0; u < 3; ++u) {
        AmsUnit unit;
        unit.unit_index = u;
        unit.slot_count = 4;
        unit.first_slot_global_index = u * 4;
        unit.topology = PathTopology::HUB;
        unit.hub_tool_label = 0; // All share T0
        for (int s = 0; s < 4; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = u * 4 + s;
            slot.mapped_tool = 0; // All map to T0
            unit.slots.push_back(slot);
        }
        info.units.push_back(unit);
    }

    info.total_slots = 12;

    auto layout = compute_system_tool_layout(info, nullptr);

    // All 3 units share one physical nozzle
    CHECK(layout.total_physical_tools == 1);

    // All units point to physical nozzle 0
    for (const auto& utl : layout.units) {
        CHECK(utl.first_physical_tool == 0);
        CHECK(utl.tool_count == 1);
    }

    // Virtual tool T0 maps to physical 0
    REQUIRE(layout.virtual_to_physical.count(0) == 1);
    CHECK(layout.virtual_to_physical.at(0) == 0);

    // Label is T0
    REQUIRE(layout.physical_to_virtual_label.size() == 1);
    CHECK(layout.physical_to_virtual_label[0] == 0);
}

TEST_CASE("SystemToolLayout: PARALLEL unit with shared mapped_tools (toolchanger+hub)",
          "[ams][tool_layout]") {
    // Toolchanger with hub: 4 lanes, 3 extruders.
    // Lanes 0,1 direct (T0, T1), lanes 2,3 share T2 via hub.
    // PARALLEL topology should produce 3 physical nozzles with T2 shared.
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;
    unit.first_slot_global_index = 0;
    unit.topology = PathTopology::PARALLEL;
    int tool_map[] = {0, 1, 2, 2};
    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = tool_map[s];
        unit.slots.push_back(slot);
    }
    info.units.push_back(unit);
    info.total_slots = 4;

    auto layout = compute_system_tool_layout(info, nullptr);

    // 3 unique tools → 3 physical nozzles
    CHECK(layout.total_physical_tools == 3);
    REQUIRE(layout.units.size() == 1);
    CHECK(layout.units[0].tool_count == 3);
    CHECK(layout.units[0].first_physical_tool == 0);

    // Virtual T0, T1, T2 each map to their own physical nozzle
    REQUIRE(layout.virtual_to_physical.count(0) == 1);
    REQUIRE(layout.virtual_to_physical.count(1) == 1);
    REQUIRE(layout.virtual_to_physical.count(2) == 1);
    CHECK(layout.virtual_to_physical.at(0) == 0);
    CHECK(layout.virtual_to_physical.at(1) == 1);
    CHECK(layout.virtual_to_physical.at(2) == 2);

    // Labels: T0, T1, T2
    REQUIRE(layout.physical_to_virtual_label.size() == 3);
    CHECK(layout.physical_to_virtual_label[0] == 0);
    CHECK(layout.physical_to_virtual_label[1] == 1);
    CHECK(layout.physical_to_virtual_label[2] == 2);
}

// ============================================================================
// PARALLEL: shared extruder deduplication (#364)
// ============================================================================

TEST_CASE("SystemToolLayout: PARALLEL shared extruder dedup via extruder_name",
          "[ams][tool_layout]") {
    // HTLF: 4 lanes, 3 extruders, 4 unique T-numbers.
    // Lanes 0,1 have unique extruders (extruder, extruder1).
    // Lanes 2,3 share extruder2 but have different T-numbers (T1, T3).
    // Should produce 3 physical tools, not 4.
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;
    unit.first_slot_global_index = 0;
    unit.topology = PathTopology::PARALLEL;

    struct LaneSpec {
        int tool;
        const char* extruder;
    };
    LaneSpec lanes[] = {
        {0, "extruder"},  // Lane 0: T0, unique extruder
        {2, "extruder1"}, // Lane 1: T2, unique extruder
        {1, "extruder2"}, // Lane 2: T1, shared extruder
        {3, "extruder2"}, // Lane 3: T3, shared extruder
    };
    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = lanes[s].tool;
        slot.extruder_name = lanes[s].extruder;
        unit.slots.push_back(slot);
    }

    info.units.push_back(unit);
    info.total_slots = 4;

    auto layout = compute_system_tool_layout(info, nullptr);

    // 3 unique extruders -> 3 physical nozzles (not 4)
    CHECK(layout.total_physical_tools == 3);
    REQUIRE(layout.units.size() == 1);
    CHECK(layout.units[0].tool_count == 3);

    // T0 (extruder) -> physical 0
    // T2 (extruder1) -> physical 1
    // T1,T3 (extruder2) -> physical 2 (shared)
    REQUIRE(layout.virtual_to_physical.count(0) == 1);
    REQUIRE(layout.virtual_to_physical.count(1) == 1);
    REQUIRE(layout.virtual_to_physical.count(2) == 1);
    REQUIRE(layout.virtual_to_physical.count(3) == 1);
    CHECK(layout.virtual_to_physical.at(0) == 0);
    CHECK(layout.virtual_to_physical.at(2) == 1);
    // T1 and T3 share the same physical position
    CHECK(layout.virtual_to_physical.at(1) == layout.virtual_to_physical.at(3));

    // Verify physical-to-virtual labels use min tool per extruder group
    REQUIRE(layout.physical_to_virtual_label.size() == 3);
    CHECK(layout.physical_to_virtual_label[0] == 0); // extruder -> T0
    CHECK(layout.physical_to_virtual_label[1] == 2); // extruder1 -> T2
    CHECK(layout.physical_to_virtual_label[2] == 1); // extruder2 -> min(T1,T3) = T1
}

// ============================================================================
// PARALLEL: cross-unit lane remapping (#363)
// ============================================================================

TEST_CASE("SystemToolLayout: PARALLEL cross-unit remap does not inflate tool count",
          "[ams][tool_layout]") {
    // Issue #363: 2 PARALLEL units with 6 lanes each. Normally Unit 0 = T0-T5,
    // Unit 1 = T6-T11. After SET_MAP, Unit 0 has a lane remapped to T11.
    // Old code: max_tool(11) - min_tool(0) + 1 = 12 tools for Unit 0 (WRONG)
    // Fixed: count distinct mapped tools = 6 (CORRECT)
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    // Unit 0: 6 lanes, lane0 remapped to T1, lane1 remapped to T0,
    // lane5 remapped to T11 (from Unit 1's range)
    {
        AmsUnit unit;
        unit.unit_index = 0;
        unit.slot_count = 6;
        unit.first_slot_global_index = 0;
        unit.topology = PathTopology::PARALLEL;
        int tool_map[] = {1, 0, 2, 3, 4, 11};
        for (int s = 0; s < 6; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = s;
            slot.mapped_tool = tool_map[s];
            unit.slots.push_back(slot);
        }
        info.units.push_back(unit);
    }

    // Unit 1: 6 lanes, T5-T10 (T11 was claimed by Unit 0's remap, T5 replaces it)
    {
        AmsUnit unit;
        unit.unit_index = 1;
        unit.slot_count = 6;
        unit.first_slot_global_index = 6;
        unit.topology = PathTopology::PARALLEL;
        int tool_map[] = {5, 6, 7, 8, 9, 10};
        for (int s = 0; s < 6; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = 6 + s;
            slot.mapped_tool = tool_map[s];
            unit.slots.push_back(slot);
        }
        info.units.push_back(unit);
    }

    info.total_slots = 12;

    auto layout = compute_system_tool_layout(info, nullptr);

    // Each unit should have exactly 6 physical tools (not 12!)
    REQUIRE(layout.units.size() == 2);
    CHECK(layout.units[0].tool_count == 6);
    CHECK(layout.units[1].tool_count == 6);
    CHECK(layout.total_physical_tools == 12);

    // Virtual-to-physical mapping should use sorted rank within each unit
    // Unit 0 sorted tools: {0, 1, 2, 3, 4, 11} → physical 0-5
    CHECK(layout.virtual_to_physical.at(0) == 0); // rank 0 of {0,1,2,3,4,11}
    CHECK(layout.virtual_to_physical.at(1) == 1); // rank 1
    CHECK(layout.virtual_to_physical.at(2) == 2);
    CHECK(layout.virtual_to_physical.at(3) == 3);
    CHECK(layout.virtual_to_physical.at(4) == 4);
    CHECK(layout.virtual_to_physical.at(11) == 5); // T11 gets rank 5, not 11

    // Unit 1 sorted tools: {5, 6, 7, 8, 9, 10} → physical 6-11
    CHECK(layout.virtual_to_physical.at(5) == 6);
    CHECK(layout.virtual_to_physical.at(6) == 7);
    CHECK(layout.virtual_to_physical.at(7) == 8);
    CHECK(layout.virtual_to_physical.at(8) == 9);
    CHECK(layout.virtual_to_physical.at(9) == 10);
    CHECK(layout.virtual_to_physical.at(10) == 11);
}

TEST_CASE("SystemToolLayout: 2 HUB units with different hub_tool_labels stay separate",
          "[ams][tool_layout]") {
    // Multi-extruder setup: unit 0 feeds T0, unit 1 feeds T1
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    for (int u = 0; u < 2; ++u) {
        AmsUnit unit;
        unit.unit_index = u;
        unit.slot_count = 4;
        unit.first_slot_global_index = u * 4;
        unit.topology = PathTopology::HUB;
        unit.hub_tool_label = u; // T0 and T1 respectively
        for (int s = 0; s < 4; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = u * 4 + s;
            slot.mapped_tool = u; // Maps to own tool
            unit.slots.push_back(slot);
        }
        info.units.push_back(unit);
    }

    info.total_slots = 8;

    auto layout = compute_system_tool_layout(info, nullptr);

    // Different hub_tool_labels = separate physical nozzles
    CHECK(layout.total_physical_tools == 2);
    CHECK(layout.units[0].first_physical_tool == 0);
    CHECK(layout.units[1].first_physical_tool == 1);
    REQUIRE(layout.physical_to_virtual_label.size() == 2);
    CHECK(layout.physical_to_virtual_label[0] == 0);
    CHECK(layout.physical_to_virtual_label[1] == 1);
}

// ============================================================================
// MIXED topology tests (lane_is_hub_routed)
// ============================================================================

TEST_CASE("SystemToolLayout: MIXED topology hub lanes share single nozzle position",
          "[ams][tool_layout][mixed]") {
    // 1 MIXED unit: 4 slots, lanes 0,1 direct, lanes 2,3 hub-routed
    // Should produce 3 physical tools (2 direct + 1 hub group), NOT 4
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;
    unit.first_slot_global_index = 0;
    unit.topology = PathTopology::MIXED;
    unit.lane_is_hub_routed = {false, false, true, true};

    int tool_map[] = {0, 2, 1, 3};
    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = tool_map[s];
        unit.slots.push_back(slot);
    }
    info.units.push_back(unit);
    info.total_slots = 4;

    // Need a backend that returns correct slot info for mapped_tool lookup
    AmsBackendMock backend(4);
    backend.set_htlf_toolchanger_mode(true);

    auto layout = compute_system_tool_layout(info, &backend);

    // 2 direct lanes + 1 hub group = 3 physical tools, NOT 4
    CHECK(layout.total_physical_tools == 3);
    REQUIRE(layout.units.size() == 1);
    CHECK(layout.units[0].tool_count == 3);
}

TEST_CASE("SystemToolLayout: HTLF MIXED + Toolchanger PARALLEL total tools",
          "[ams][tool_layout][mixed][htlf]") {
    AmsBackendMock backend(4);
    backend.set_htlf_toolchanger_mode(true);

    auto info = backend.get_system_info();

    auto layout = compute_system_tool_layout(info, &backend);

    // HTLF: 2 direct + 1 hub group = 3, Toolchanger: 3 parallel = 3, total = 6
    CHECK(layout.total_physical_tools == 6);
    REQUIRE(layout.units.size() == 2);
    CHECK(layout.units[0].tool_count == 3); // HTLF MIXED
    CHECK(layout.units[1].tool_count == 3); // Toolchanger PARALLEL

    // Virtual→physical mapping:
    // HTLF direct lanes sorted: T0→phys0, T2→phys1, hub group (T1)→phys2
    CHECK(layout.virtual_to_physical.at(0) == 0); // T0 direct → phys 0
    CHECK(layout.virtual_to_physical.at(2) == 1); // T2 direct → phys 1
    CHECK(layout.virtual_to_physical.at(1) == 2); // T1 hub → phys 2

    // Toolchanger: T4→phys3, T5→phys4, T6→phys5
    CHECK(layout.virtual_to_physical.at(4) == 3);
    CHECK(layout.virtual_to_physical.at(5) == 4);
    CHECK(layout.virtual_to_physical.at(6) == 5);
}

TEST_CASE("SystemToolLayout: MIXED with no hub lanes gives tool_count == slot_count",
          "[ams][tool_layout][mixed][edge]") {
    // MIXED unit with lane_is_hub_routed all false — degenerates to PARALLEL-like
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;
    unit.first_slot_global_index = 0;
    unit.topology = PathTopology::MIXED;
    unit.lane_is_hub_routed = {false, false, false, false}; // All direct

    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = s;
        unit.slots.push_back(slot);
    }
    info.units.push_back(unit);
    info.total_slots = 4;

    auto layout = compute_system_tool_layout(info, nullptr);

    // All 4 lanes direct → 4 physical tools (same as PARALLEL)
    CHECK(layout.total_physical_tools == 4);
    REQUIRE(layout.units.size() == 1);
    CHECK(layout.units[0].tool_count == 4);
}

TEST_CASE("SystemToolLayout: MIXED with all hub lanes gives tool_count 1",
          "[ams][tool_layout][mixed][edge]") {
    // MIXED unit with lane_is_hub_routed all true — degenerates to HUB-like
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    AmsUnit unit;
    unit.unit_index = 0;
    unit.slot_count = 4;
    unit.first_slot_global_index = 0;
    unit.topology = PathTopology::MIXED;
    unit.lane_is_hub_routed = {true, true, true, true}; // All hub

    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = s;
        unit.slots.push_back(slot);
    }
    info.units.push_back(unit);
    info.total_slots = 4;

    auto layout = compute_system_tool_layout(info, nullptr);

    // All 4 lanes hub → 1 physical tool (same as HUB)
    CHECK(layout.total_physical_tools == 1);
    REQUIRE(layout.units.size() == 1);
    CHECK(layout.units[0].tool_count == 1);
}

// ============================================================================
// Multi-unit mock regression: single toolhead must produce 1 nozzle
// ============================================================================

TEST_CASE("SystemToolLayout: multi-unit mock produces 1 physical tool", "[ams][tool_layout]") {
    // Regression test: multi-unit mock (Box Turtle + Night Owl) both feed
    // a single T0 toolhead via hub. hub_tool_label must be set so both
    // units share one physical nozzle position.
    AmsBackendMock backend(4);
    backend.set_multi_unit_mode(true);

    auto info = backend.get_system_info();

    auto layout = compute_system_tool_layout(info, &backend);

    // Single toolhead — must be exactly 1 physical tool
    CHECK(layout.total_physical_tools == 1);

    // Both units should share the same physical nozzle
    REQUIRE(layout.units.size() == 2);
    CHECK(layout.units[0].tool_count == 1);
    CHECK(layout.units[1].tool_count == 1);
    CHECK(layout.units[0].first_physical_tool == 0);
    CHECK(layout.units[1].first_physical_tool == 0);

    // Label should be T0
    REQUIRE(layout.physical_to_virtual_label.size() == 1);
    CHECK(layout.physical_to_virtual_label[0] == 0);
}

TEST_CASE("SystemToolLayout: 2 HUB units sharing hub_tool_label=0 merge to 1 nozzle",
          "[ams][tool_layout]") {
    // Explicit test: 2 HUB units with hub_tool_label=0 on both must
    // share a single physical nozzle position.
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    for (int u = 0; u < 2; ++u) {
        AmsUnit unit;
        unit.unit_index = u;
        unit.slot_count = (u == 0) ? 4 : 2;
        unit.first_slot_global_index = (u == 0) ? 0 : 4;
        unit.topology = PathTopology::HUB;
        unit.hub_tool_label = 0;
        for (int s = 0; s < unit.slot_count; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = unit.first_slot_global_index + s;
            slot.mapped_tool = 0;
            unit.slots.push_back(slot);
        }
        info.units.push_back(unit);
    }
    info.total_slots = 6;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 1);
    REQUIRE(layout.units.size() == 2);
    CHECK(layout.units[0].first_physical_tool == 0);
    CHECK(layout.units[1].first_physical_tool == 0);
    CHECK(layout.units[0].tool_count == 1);
    CHECK(layout.units[1].tool_count == 1);
}

// ============================================================================
// Cross-unit head sharing (multiACE: an ACE feeds a head the U1 already owns)
// ============================================================================

TEST_CASE("SystemToolLayout: a unit feeding an already-owned head shares its nozzle",
          "[ams][tool_layout][multiace]") {
    // Snapmaker U1 + multiACE in head mode. Unit 0 is the four toolheads; unit 1
    // is an ACE whose four bays all feed head 3. The ACE is a SOURCE for T3, not
    // a fifth head — before this, the overview drew five nozzles and labelled T3
    // twice on a four-head machine.
    AmsSystemInfo info;
    info.type = AmsType::MULTIACE;

    AmsUnit u1;
    u1.unit_index = 0;
    u1.slot_count = 4;
    u1.topology = PathTopology::PARALLEL;
    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = s;
        slot.extruder_name = (s == 0) ? "extruder" : ("extruder" + std::to_string(s));
        u1.slots.push_back(slot);
    }
    info.units.push_back(u1);

    AmsUnit ace;
    ace.unit_index = 1;
    ace.slot_count = 4;
    ace.first_slot_global_index = 4;
    ace.topology = PathTopology::HUB; // head mode: four bays, one head
    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = 4 + s;
        slot.mapped_tool = 3; // every bay feeds T3
        ace.slots.push_back(slot);
    }
    info.units.push_back(ace);
    info.total_slots = 8;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 4); // four heads, not five
    REQUIRE(layout.units.size() == 2);
    CHECK(layout.units[1].first_physical_tool == 3); // the ACE points AT T3
    CHECK(layout.units[1].tool_count == 1);
    CHECK(layout.virtual_to_physical.at(3) == 3);
}

TEST_CASE("SystemToolLayout: multi mode shares every head with the U1",
          "[ams][tool_layout][multiace]") {
    // The same rig with the ACE in multi mode: bay s feeds head s. All four
    // heads are already owned, so the ACE still adds no nozzles.
    AmsSystemInfo info;
    info.type = AmsType::MULTIACE;

    for (int u = 0; u < 2; ++u) {
        AmsUnit unit;
        unit.unit_index = u;
        unit.slot_count = 4;
        unit.first_slot_global_index = u * 4;
        unit.topology = PathTopology::PARALLEL;
        for (int s = 0; s < 4; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = u * 4 + s;
            slot.mapped_tool = s;
            if (u == 0) {
                slot.extruder_name = (s == 0) ? "extruder" : ("extruder" + std::to_string(s));
            }
            unit.slots.push_back(slot);
        }
        info.units.push_back(unit);
    }
    info.total_slots = 8;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 4);
    CHECK(layout.units[1].first_physical_tool == 0);
    CHECK(layout.units[1].tool_count == 4);
}

TEST_CASE("SystemToolLayout: a unit adding a new head still allocates",
          "[ams][tool_layout][multiace]") {
    // The guard that keeps ordinary multi-unit rigs untouched: unit 1 feeds one
    // head unit 0 owns AND one it does not, so it is not purely a source and
    // must allocate for the new head rather than silently sharing.
    AmsSystemInfo info;
    info.type = AmsType::AFC;

    AmsUnit a;
    a.unit_index = 0;
    a.slot_count = 2;
    a.topology = PathTopology::PARALLEL;
    for (int s = 0; s < 2; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = s; // heads 0, 1
        a.slots.push_back(slot);
    }
    info.units.push_back(a);

    AmsUnit b;
    b.unit_index = 1;
    b.slot_count = 2;
    b.first_slot_global_index = 2;
    b.topology = PathTopology::PARALLEL;
    for (int s = 0; s < 2; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = 2 + s;
        slot.mapped_tool = 1 + s; // head 1 (shared) and head 2 (new)
        b.slots.push_back(slot);
    }
    info.units.push_back(b);
    info.total_slots = 4;

    auto layout = compute_system_tool_layout(info, nullptr);

    CHECK(layout.total_physical_tools == 4); // 2 + 2, allocated as before
}

// ============================================================================
// feeds_mask: which of its nozzles a unit actually supplies
// ============================================================================

namespace {
/// A backend that answers only the two questions the mask needs: which slots
/// are owned by another unit, and each unit's topology.
class OwnedHeadsBackend : public AmsBackendMock {
  public:
    OwnedHeadsBackend() : AmsBackendMock(4) {}
    std::map<int, int> owner_of; ///< global slot -> owning unit index
    std::vector<PathTopology> topo;

    [[nodiscard]] std::optional<int> slot_identity_owner_unit(int slot_index) const override {
        auto it = owner_of.find(slot_index);
        return it == owner_of.end() ? std::nullopt : std::optional<int>(it->second);
    }
    [[nodiscard]] PathTopology get_unit_topology(int unit_index) const override {
        return unit_index >= 0 && unit_index < static_cast<int>(topo.size())
                   ? topo[static_cast<size_t>(unit_index)]
                   : PathTopology::PARALLEL;
    }
};

/// U1 (four heads) plus `ace_count` head-mode ACEs; ACE k feeds head fed[k].
AmsSystemInfo u1_with_aces(const std::vector<int>& fed) {
    AmsSystemInfo info;
    info.type = AmsType::MULTIACE;
    AmsUnit u1;
    u1.unit_index = 0;
    u1.slot_count = 4;
    u1.topology = PathTopology::PARALLEL;
    for (int s = 0; s < 4; ++s) {
        SlotInfo slot;
        slot.slot_index = s;
        slot.global_index = s;
        slot.mapped_tool = s;
        u1.slots.push_back(slot);
    }
    info.units.push_back(u1);
    int next = 4;
    for (size_t k = 0; k < fed.size(); ++k) {
        AmsUnit ace;
        ace.unit_index = static_cast<int>(k) + 1;
        ace.slot_count = 4;
        ace.first_slot_global_index = next;
        ace.topology = PathTopology::HUB;
        for (int s = 0; s < 4; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = next++;
            slot.mapped_tool = fed[k];
            ace.slots.push_back(slot);
        }
        info.units.push_back(ace);
    }
    info.total_slots = next;
    return info;
}
} // namespace

TEST_CASE("SystemToolLayout: feeds_mask clears the nozzles a unit does not supply",
          "[ams][tool_layout][multiace]") {
    // The U1 owns four heads; an ACE bound to head 3 supplies it. The U1's mask
    // must drop bit 3 -- otherwise the overview draws a U1 feeder line AND the
    // ACE's line to one nozzle, saying two things feed it -- while the ACE, a
    // source for its own bays, keeps its one bit.
    OwnedHeadsBackend backend;
    backend.topo = {PathTopology::PARALLEL, PathTopology::HUB};
    backend.owner_of[3] = 1; // head 3's identity belongs to the ACE (unit 1)
    const auto info = u1_with_aces({3});

    auto layout = compute_system_tool_layout(info, &backend);
    REQUIRE(layout.units.size() == 2);
    CHECK(layout.units[0].feeds_mask == 0b0111u);
    CHECK(layout.units[1].feeds_mask == 0b1u);
}

TEST_CASE("SystemToolLayout: a unit feeding NONE of its heads gets a mask of 0",
          "[ams][tool_layout][multiace]") {
    // Four ACEs, one per head: the U1 supplies nothing. 0 is the true answer
    // and must be delivered as such -- the canvas used to read 0 as "unset"
    // and drew every U1 feeder line, the exact double-feed the mask prevents.
    OwnedHeadsBackend backend;
    backend.topo = {PathTopology::PARALLEL, PathTopology::HUB, PathTopology::HUB, PathTopology::HUB,
                    PathTopology::HUB};
    for (int h = 0; h < 4; ++h) {
        backend.owner_of[h] = h + 1;
    }
    const auto info = u1_with_aces({0, 1, 2, 3});

    auto layout = compute_system_tool_layout(info, &backend);
    REQUIRE(layout.units.size() == 5);
    CHECK(layout.units[0].feeds_mask == 0u);
    for (size_t k = 1; k < 5; ++k) {
        INFO("ACE unit " << k);
        CHECK(layout.units[k].feeds_mask == 0b1u);
    }
}

TEST_CASE("SystemToolLayout: feeds_mask is all-set with no backend or no ownership",
          "[ams][tool_layout]") {
    // Every ordinary system: nothing is owned elsewhere, every nozzle a unit
    // has it also feeds.
    const auto info = u1_with_aces({3});
    auto layout = compute_system_tool_layout(info, nullptr);
    REQUIRE(layout.units.size() == 2);
    CHECK(layout.units[0].feeds_mask == 0b1111u);
    CHECK(layout.units[1].feeds_mask == 0b1u);

    OwnedHeadsBackend backend;
    backend.topo = {PathTopology::PARALLEL, PathTopology::HUB};
    layout = compute_system_tool_layout(info, &backend);
    CHECK(layout.units[0].feeds_mask == 0b1111u);
}
