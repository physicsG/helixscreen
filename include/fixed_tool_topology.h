// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Firmware-fixed toolheads.
//
// Most multi-tool printers declare their tools as Klipper objects (klipper-
// toolchanger's `tool T<n>`), and ToolState enumerates them from discovery. Some
// machines ship a fixed set of toolheads that publish no such objects: the heads
// are known from the machine itself, each with its own extruder, heater and part
// fan. This module is the ONLY place that knows which machines those are and what
// their heads are called. ToolState asks it and never names a vendor.
//
// Adding a machine means adding one Provider to the table in
// fixed_tool_topology.cpp - no call site changes.

#include <string>
#include <vector>

namespace helix {
class PrinterDiscovery;
}

namespace helix::tool_topology {

/// One fixed toolhead's Klipper object names.
struct FixedTool {
    std::string extruder;
    std::string heater;
    std::string fan;
};

/// The printer's fixed toolheads in tool order; empty when the printer declares
/// its tools some other way.
std::vector<FixedTool> fixed_toolheads(const PrinterDiscovery& hw);

/// Name of the matched provider for logs; "none" when nothing matched.
const char* provider_name(const PrinterDiscovery& hw);

} // namespace helix::tool_topology
