// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file ams_state_tool_mapping.cpp
 * @brief AmsState: the tool -> lane mapping questions: available slots, seeds, routing and render
 * colors
 *
 * One of the files AmsState's definitions are split across by concern; the
 * class and its threading contract are in ams_state.h.
 */

#include "ams_remap.h"
#include "ams_state.h"
#include "ams_state_internal.h"
#include "ams_tool_topology.h"
#include "filament_mapper.h"
#include "settings_manager.h"

#include <spdlog/spdlog.h>

namespace helix {
using ams_state_detail::assert_main_thread;

// Declared in ams_tool_topology.h; see there for what nullopt means to callers.
std::optional<helix::ToolTopology> build_ams_topology(AmsBackend* backend, int backend_index) {
    if (!backend)
        return std::nullopt;
    // Table ownership, NOT remap capability. Two different questions that part
    // company on the U1: it can remap and owns no table.
    if (!backend->owns_tool_mapping_table())
        return std::nullopt;

    std::vector<int> mapping = backend->get_tool_mapping();
    if (mapping.empty()) {
        // Fallback: default 1:1 from slot count
        int n = backend->get_system_info().total_slots;
        mapping.resize(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            mapping[static_cast<size_t>(i)] = i;
    }

    helix::ToolTopology topo;
    topo.tool_count = static_cast<int>(mapping.size());
    topo.tool_to_slot = std::move(mapping);
    topo.active_tool = backend->get_current_tool();
    topo.allows_empty_carriage = backend->load_mounts_tool();
    topo.backend_index = backend_index;
    return topo;
}

std::vector<uint32_t> AmsState::routed_tool_colors() const {
    auto* backend = get_backend();
    if (!backend) {
        spdlog::debug("[AmsState] routed_tool_colors: no AMS backend");
        return {};
    }

    // The applied routing, asked as a capability question. A backend that tracks
    // a separate firmware routing table (Snapmaker U1's extruder_map_table)
    // answers from it; one whose physical map IS the routing (AFC, Happy Hare,
    // a plain tool changer) answers from that. Vendor knowledge stays inside the
    // backend — nothing here names a firmware.
    // Whether the attachment map may stand in when the backend publishes no
    // routing is a real decision, so it is a named one (FilamentMapper::
    // effective_routing) rather than an `if` here: on a tool changer that map is
    // which slot each head owns, never which head prints a tool, and letting it
    // stand in silently converts "no opinion" into a confident identity answer.
    std::vector<int> routing = helix::FilamentMapper::effective_routing(
        backend->get_tool_mapping(), backend->get_system_info().tool_to_slot_map,
        /*attachment_is_routing=*/!is_tool_changer(backend->get_type()));

    auto colors = helix::FilamentMapper::routed_tool_colors(routing, collect_available_slots(),
                                                            backend->tool_mapping_origin());
    spdlog::debug("[AmsState] routed_tool_colors: {} routing entries -> {} color(s)",
                  routing.size(), colors.size());
    return colors;
}

std::vector<helix::AvailableSlot> AmsState::collect_available_slots() const {
    std::vector<helix::AvailableSlot> slots;
    assert_main_thread();

    const auto& backends = registry_.backends();
    for (size_t bi = 0; bi < backends.size(); ++bi) {
        const auto& backend = backends[bi];
        if (!backend) {
            continue;
        }

        auto info = backend->get_system_info();
        bool multi_unit = info.units.size() > 1;

        for (const auto& unit : info.units) {
            if (unit.absent) {
                continue;
            }
            for (const auto& slot_info : unit.slots) {
                helix::AvailableSlot as;
                as.slot_index = slot_info.global_index;
                as.local_slot_index = slot_info.slot_index;
                as.backend_index = static_cast<int>(bi);
                as.color_rgb = slot_info.color_rgb;
                as.multi_color_hexes = slot_info.multi_color_hexes;
                as.material = slot_info.material;
                as.is_empty = (slot_info.status == SlotStatus::EMPTY ||
                               slot_info.status == SlotStatus::UNKNOWN);
                as.remaining_weight_g = slot_info.remaining_weight_g;
                as.current_tool_mapping = slot_info.mapped_tool;
                as.unit_index = unit.unit_index;
                as.noun = backend->lane_noun();
                if (multi_unit) {
                    as.unit_display_name =
                        unit.display_name.empty() ? unit.name : unit.display_name;
                }
                slots.push_back(std::move(as));
            }
        }
    }

    spdlog::debug("[AmsState] Collected {} available slots from {} backends", slots.size(),
                  backends.size());
    return slots;
}

std::vector<helix::ToolMapping>
AmsState::seed_tool_mappings(const std::vector<helix::GcodeToolInfo>& tools,
                             const std::vector<helix::AvailableSlot>& slots) const {
    return seed_tool_mappings(tools, slots, effective_auto_match());
}

std::vector<helix::ToolMapping>
AmsState::seed_tool_mappings(const std::vector<helix::GcodeToolInfo>& tools,
                             const std::vector<helix::AvailableSlot>& slots,
                             bool auto_color_map) const {
    return helix::FilamentMapper::effective_mappings(tools, slots, auto_color_map,
                                                     collect_firmware_routing());
}

helix::FirmwareRouting AmsState::collect_firmware_routing() const {
    assert_main_thread();
    if (auto* backend = get_backend(0)) {
        return backend->firmware_default_routing();
    }
    return helix::FirmwareRouting::identity();
}

bool AmsState::effective_auto_match() const {
    assert_main_thread();
    // Ask whether the user's choice can be carried out at all, not whether the
    // mapping card is editable. Editability is only one of the two routes, and
    // the backend it gets wrong is the one where the toggle does the most
    // damage: the U1 honors every pick through its pre-print send, so pinning
    // auto-match there left color proximity rewriting the print's routing with
    // no way for the user to decline it.
    bool user_choice_honored = false;
    if (auto* backend = get_backend(0)) {
        user_choice_honored = helix::printer::can_remap(*backend);

        // HOLD — kPreprintSeedFollowsUserSetting (ams_state.h) carries the full
        // reasoning and the hardware evidence that would lift it. A backend that
        // cannot remap PERSISTENTLY can only have answered true through its
        // pre-print send, so this is exactly the case being held, and putting it
        // back leaves the U1 seeding as it does today. The predicate itself is
        // untouched: it is still the one rule the print-start warning shares, and
        // gating it there would resurrect a false toast on the U1.
        if (!kPreprintSeedFollowsUserSetting &&
            !(helix::printer::remap_is_persistent(backend->get_remap_strategy()) &&
              backend->remap_ready())) {
            user_choice_honored = false;
        }
    }
    return !user_choice_honored || helix::SettingsManager::instance().get_auto_color_map();
}
} // namespace helix
