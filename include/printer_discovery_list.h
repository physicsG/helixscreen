// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "mdns_discovery.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

/**
 * @file printer_discovery_list.h
 * @brief The discovered-printers dropdown shared by the setup wizard and the host modal
 *
 * The dropdown's first row is a prompt, so picking the only printer found is still a
 * change of selection; row i + 1 is printer i.
 */
namespace helix {

/// mDNS results first, then sweep results at an address mDNS did not name. One entry
/// per ip:port; mDNS wins a tie because it carries the printer's name.
std::vector<DiscoveredPrinter>
merge_discovered_printers(const std::vector<DiscoveredPrinter>& mdns,
                          const std::vector<DiscoveredPrinter>& sweep);

/// "voron (192.168.1.112:7125)", or "192.168.1.52:80" for a printer with no name.
std::string discovered_printer_label(const DiscoveredPrinter& printer);

/// Newline-separated dropdown options: the prompt row, then one row per printer.
/// @param searching Discovery is still looking, which the prompt says when nothing is found
std::string discovered_printer_options(const std::vector<DiscoveredPrinter>& printers,
                                       bool searching);

/// The printer behind dropdown row @p selected, or nullptr for the prompt row.
const DiscoveredPrinter* discovered_printer_at(const std::vector<DiscoveredPrinter>& printers,
                                               uint32_t selected);

/// Where the setup wizard and the host modal get their printer discovery. Unset (tests,
/// --test, the firmware) means no discovery runs and the dropdown stays hidden.
void set_printer_discovery_source(std::function<std::unique_ptr<IMdnsDiscovery>()> source);

/// A new discovery from the source, or nullptr when none is set.
std::unique_ptr<IMdnsDiscovery> make_printer_discovery();

} // namespace helix
