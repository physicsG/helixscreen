// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>
#include <vector>

namespace helix {

/**
 * @brief The physical head a logical tool routes to with NO remap applied.
 *
 * This is the firmware's own DEFAULT map: what it routes a print by when
 * nothing remaps it for that print. Three AMS shapes exist and they disagree:
 *
 *   - lane-per-tool (AFC, Happy Hare, klipper-toolchanger, CFS, QIDI, ACE,
 *     OpenAMS): tool N owns lane N unless a standing table on the printer says
 *     otherwise. An AFC, Happy Hare or CFS remap, an OpenAMS group's serving
 *     slot and an ACEPRO slot's tool number are tables the firmware routes by,
 *     so that table is the default (AmsBackend::firmware_default_routing()).
 *   - fixed-head (Snapmaker U1): four physical heads and up to 32 logical
 *     tools, so the table is [0,1,2,3,0,0,...] - measured live on a U1.
 *   - table-driven (FlashForge AD5X IFS): an arbitrary 16-entry tool->port map
 *     that the firmware publishes and the user can change.
 *
 * Hardcoding any one of these in shared code makes the others silently wrong,
 * which is exactly what happened: a four-head constant sent every AFC tool
 * above 3 to lane 0. Backends declare their own shape via
 * AmsBackend::firmware_default_routing().
 *
 * Deliberately a plain value with no AMS dependency, so FilamentMapper stays
 * pure and every rule over it is testable without a backend.
 *
 * @note Not always AmsSystemInfo::tool_to_slot_map. That is the default only
 *       where the firmware publishes and keeps it; a U1 freezes it at 1:1 while
 *       its real map lives elsewhere, and a per-print map is not a default.
 */
struct FirmwareRouting {
    /// head_for_tool[tool] = physical head, or -1 for "this tool has no head".
    /// EMPTY means identity - every tool routes to its own index.
    std::vector<int> head_for_tool;

    /// Head for tools past the end of the table. -1 means unmapped.
    int fallback_head = -1;

    /// The firmware picks each tool's lane itself when the print starts, so no
    /// tool has a head before then and nothing may claim one.
    bool chosen_at_print = false;

    /// Physical head for @p tool, or -1 when the tool has none.
    [[nodiscard]] int head(int tool) const {
        if (tool < 0 || chosen_at_print) {
            return -1;
        }
        if (head_for_tool.empty()) {
            return tool; // identity
        }
        if (tool < static_cast<int>(head_for_tool.size())) {
            return head_for_tool[static_cast<size_t>(tool)];
        }
        return fallback_head;
    }

    /// Lane-per-tool: tool N owns lane N, with no upper bound.
    [[nodiscard]] static FirmwareRouting identity() {
        return {};
    }

    /// No tool has a head until the firmware picks one at print time.
    [[nodiscard]] static FirmwareRouting picked_at_print() {
        FirmwareRouting routing;
        routing.chosen_at_print = true;
        return routing;
    }

    /// A standing tool -> head table: @p table where it names any tool, tools
    /// past its end unmapped; identity where it is empty.
    [[nodiscard]] static FirmwareRouting from_table(std::vector<int> table) {
        if (table.empty()) {
            return identity();
        }
        FirmwareRouting routing;
        routing.head_for_tool = std::move(table);
        routing.fallback_head = -1;
        return routing;
    }

    /// Fixed-head machine: heads 0..@p head_count-1, everything beyond on
    /// @p fallback. The Snapmaker U1 is fixed_heads(4, 0).
    [[nodiscard]] static FirmwareRouting fixed_heads(int head_count, int fallback = 0) {
        FirmwareRouting routing;
        routing.head_for_tool.reserve(static_cast<size_t>(head_count > 0 ? head_count : 0));
        for (int i = 0; i < head_count; ++i) {
            routing.head_for_tool.push_back(i);
        }
        routing.fallback_head = fallback;
        return routing;
    }
};

} // namespace helix
