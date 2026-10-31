// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Chamber air circulation modes.
//
// Some enclosed printers route chamber air one of two ways with a set of fans
// the firmware drives together: recirculating it inside the enclosure (heat
// retention for ABS/ASA-class materials) or exhausting it out of the machine
// (cooling for PLA/TPU), plus an off state. The firmware owns which fans run at
// which speed for each mode; HelixScreen only names the mode and sends it.
//
// This module is the ONLY place that knows which printers have such modes and
// what each one sends. The fan screen asks these questions and never names the
// firmware. Adding a firmware means adding one Provider to the table in
// chamber_air_modes.cpp - no call site changes.
//
// No provider reports the active mode. A mode is a macro that sets several
// fans once; the fans stay individually controllable afterwards, so their
// speeds cannot be read back as a mode, and any guess would show a selection
// the printer no longer holds. The UI therefore offers modes as actions.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace helix {
class PrinterDiscovery;
}

namespace helix::chamber_air {

/// The fixed set of modes a printer may offer.
enum class AirMode {
    Recirculate, ///< Keep chamber air inside the enclosure
    Exhaust,     ///< Vent chamber air out of the machine
    Off,         ///< Every chamber circulation fan off
};

/// Whether this printer offers any chamber air mode.
bool supported(const PrinterDiscovery& hw);

/// The modes this printer offers, in AirMode order. Empty when unsupported.
std::vector<AirMode> modes(const PrinterDiscovery& hw);

/// Whether this printer offers @p mode.
bool offers(const PrinterDiscovery& hw, AirMode mode);

/// Gcode that switches the chamber to @p mode, or an empty string when the
/// printer does not offer it.
std::string mode_gcode(const PrinterDiscovery& hw, AirMode mode);

/// Stable identifier for @p mode ("recirculate", "exhaust", "off"), as XML
/// event user_data names it.
const char* mode_id(AirMode mode);

/// The mode an identifier names, or nullopt for anything else.
std::optional<AirMode> parse_mode_id(std::string_view id);

/// Human-readable name of the matched firmware, for logging. Empty when none.
std::string provider_name(const PrinterDiscovery& hw);

} // namespace helix::chamber_air
