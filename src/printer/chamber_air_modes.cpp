// SPDX-License-Identifier: GPL-3.0-or-later

#include "chamber_air_modes.h"

#include "printer_discovery.h"

#include <array>
#include <cstddef>

namespace helix::chamber_air {
namespace {

constexpr size_t kModeCount = 3;

constexpr std::array<AirMode, kModeCount> kAllModes = {AirMode::Recirculate, AirMode::Exhaust,
                                                       AirMode::Off};

/// One firmware that offers chamber air modes.
struct Provider {
    const char* name;
    /// Identifies the firmware from discovered hardware.
    bool (*detect)(const PrinterDiscovery& hw);
    /// Macro per mode, indexed by AirMode; nullptr where it has none. A mode is
    /// offered only while its macro is actually defined.
    std::array<const char*, kModeCount> macros;
};

// --- Z-Mod on the FlashForge Creator 5 Pro -----------------------------------
//
// c5pro_generic.cfg: AIR_CIRCULATION_INTERNAL runs chamber_heat_fan 0.9 and
// chamber_loop_fan 0.3 with the exhaust fans off ("ABS");
// AIR_CIRCULATION_EXTERNAL runs chamber_fan 0.5 and chamber_cool_fan 0.7 with
// heat/loop off ("PLA, TPU"); AIR_CIRCULATION_STOP zeroes all four. The macros
// themselves are the capability, whatever machine defines them.
bool air_circulation_detect(const PrinterDiscovery& hw) {
    return hw.has_macro("AIR_CIRCULATION_INTERNAL") || hw.has_macro("AIR_CIRCULATION_EXTERNAL") ||
           hw.has_macro("AIR_CIRCULATION_STOP");
}

const Provider kProviders[] = {
    {"Z-Mod AIR_CIRCULATION",
     air_circulation_detect,
     {"AIR_CIRCULATION_INTERNAL", "AIR_CIRCULATION_EXTERNAL", "AIR_CIRCULATION_STOP"}},
};

const Provider* match(const PrinterDiscovery& hw) {
    for (const auto& p : kProviders) {
        if (p.detect(hw)) {
            return &p;
        }
    }
    return nullptr;
}

const char* macro_for(const PrinterDiscovery& hw, AirMode mode) {
    const Provider* p = match(hw);
    if (!p) {
        return nullptr;
    }
    const char* macro = p->macros[static_cast<size_t>(mode)];
    return macro && hw.has_macro(macro) ? macro : nullptr;
}

} // namespace

bool offers(const PrinterDiscovery& hw, AirMode mode) {
    return macro_for(hw, mode) != nullptr;
}

std::vector<AirMode> modes(const PrinterDiscovery& hw) {
    std::vector<AirMode> out;
    for (AirMode m : kAllModes) {
        if (offers(hw, m)) {
            out.push_back(m);
        }
    }
    return out;
}

bool supported(const PrinterDiscovery& hw) {
    return !modes(hw).empty();
}

std::string mode_gcode(const PrinterDiscovery& hw, AirMode mode) {
    const char* macro = macro_for(hw, mode);
    return macro ? macro : "";
}

const char* mode_id(AirMode mode) {
    switch (mode) {
    case AirMode::Recirculate:
        return "recirculate";
    case AirMode::Exhaust:
        return "exhaust";
    case AirMode::Off:
        return "off";
    }
    return "";
}

std::optional<AirMode> parse_mode_id(std::string_view id) {
    for (AirMode m : kAllModes) {
        if (id == mode_id(m)) {
            return m;
        }
    }
    return std::nullopt;
}

std::string provider_name(const PrinterDiscovery& hw) {
    const Provider* p = match(hw);
    return p ? p->name : "";
}

} // namespace helix::chamber_air
