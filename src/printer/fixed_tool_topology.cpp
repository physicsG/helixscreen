// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixed_tool_topology.h"

#include "printer_discovery.h"

#include <array>

namespace helix::tool_topology {
namespace {

/// One machine family with a fixed set of toolheads.
struct Provider {
    const char* name;
    /// Whether this printer is that machine.
    bool (*detect)(const PrinterDiscovery& hw);
    /// The heads, in tool order.
    std::vector<FixedTool> (*tools)();
};

std::vector<FixedTool> snapmaker_u1_tools() {
    return {
        {"extruder", "extruder", "fan"},
        {"extruder1", "extruder1", "fan_generic e1_fan"},
        {"extruder2", "extruder2", "fan_generic e2_fan"},
        {"extruder3", "extruder3", "fan_generic e3_fan"},
    };
}

const std::array<Provider, 1> kProviders = {{
    {"Snapmaker U1", [](const PrinterDiscovery& hw) { return hw.has_snapmaker(); },
     snapmaker_u1_tools},
}};

const Provider* match(const PrinterDiscovery& hw) {
    for (const Provider& provider : kProviders) {
        if (provider.detect(hw)) {
            return &provider;
        }
    }
    return nullptr;
}

} // namespace

std::vector<FixedTool> fixed_toolheads(const PrinterDiscovery& hw) {
    const Provider* provider = match(hw);
    return provider ? provider->tools() : std::vector<FixedTool>{};
}

const char* provider_name(const PrinterDiscovery& hw) {
    const Provider* provider = match(hw);
    return provider ? provider->name : "none";
}

} // namespace helix::tool_topology
