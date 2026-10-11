// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_bypass_policy.h"

#include "printer_discovery.h"
#include "settings_manager.h"

namespace helix {

namespace {

/// The conventional wrapper names on hardware whose filament path has a master
/// switch but no bypass command. An owner whose macros are named differently
/// says so in settings.
constexpr const char* kConventionalBypassOn = "ACE_BYPASS_ON";
constexpr const char* kConventionalBypassOff = "ACE_BYPASS_OFF";

/// Sentinel meaning "whatever discovery found", matching the feeder picker.
constexpr const char* kAutoMacro = "auto";

std::string resolve_one(const PrinterDiscovery& hw, const std::string& override_name,
                        const char* conventional) {
    if (!override_name.empty() && override_name != kAutoMacro) {
        return override_name;
    }
    return hw.has_macro(conventional) ? std::string(conventional) : std::string{};
}

} // namespace

BypassMacros resolve_bypass_macros(const PrinterDiscovery& hw, const std::string& on_override,
                                   const std::string& off_override) {
    BypassMacros macros{resolve_one(hw, on_override, kConventionalBypassOn),
                        resolve_one(hw, off_override, kConventionalBypassOff)};

    // Half a pair cannot round-trip: engaging bypass with no way back strands
    // the machine in a state the UI offers no exit from.
    if (macros.on.empty() || macros.off.empty()) {
        return {};
    }
    return macros;
}

BypassMacros resolve_bypass_macros_for(const PrinterDiscovery& hw) {
    auto& settings = SettingsManager::instance();
    return resolve_bypass_macros(hw, settings.get_ace_bypass_on_macro(),
                                 settings.get_ace_bypass_off_macro());
}

bool bypass_available_for(bool firmware_supports_bypass) {
    if (firmware_supports_bypass) {
        return true; // no need to touch settings when the firmware already says yes
    }
    return bypass_available(firmware_supports_bypass,
                            SettingsManager::instance().get_ams_force_bypass_controls());
}

} // namespace helix
