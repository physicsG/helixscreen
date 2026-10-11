// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Compiled into every build (src/system/cli_args.cpp includes it), mock or not:
// header-only, std and text_io.h only.

#include "text_io.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace helix::mock {

enum class PrinterType {
    VORON_24,                 // Voron 2.4 (CoreXY, chamber heating)
    VORON_TRIDENT,            // Voron Trident (3Z, CoreXY)
    CREALITY_K1,              // Creality K1/K1C (bed slinger style)
    CREALITY_K1_MAX,          // Creality K1 Max (the #1282 CFS capture machine)
    FLASHFORGE_AD5M,          // FlashForge Adventurer 5M (enclosed)
    FLASHFORGE_CREATOR5,      // FlashForge Creator 5 Pro (4-head tool changer)
    FLASHFORGE_CREATOR5_ZMOD, // FlashForge Creator 5 Pro on Z-Mod (no klipper-toolchanger)
    GENERIC_COREXY,           // Generic CoreXY printer
    GENERIC_BEDSLINGER,       // Generic i3-style printer
    MULTI_EXTRUDER,           // Multi-extruder test case (2 extruders)
    DELTA,                    // Generic linear delta (every axis homes together)
    ELEGOO_CC1,               // Elegoo Centauri Carbon on COSMOS (load-cell probe)
    FLASHFORGE_AD5X,          // FlashForge Adventurer 5X (IFS, simulated by the mock AMS)
    CREALITY_K2_PLUS,         // Creality K2 Plus (CFS box, chamber heater)
    SNAPMAKER_U1,             // Snapmaker U1 (4 independent extruders)
};

/// Bit set of the objects every mock persona inherits by default.
using DefaultObjects = std::uint32_t;
namespace default_object {
inline constexpr DefaultObjects NONE = 0;
inline constexpr DefaultObjects HAPPY_HARE_MMU = 1u << 0; // "mmu"
/// Probe objects when HELIX_MOCK_PROBE_TYPE is unset.
inline constexpr DefaultObjects CARTOGRAPHER = 1u << 1;
inline constexpr DefaultObjects BME280_CHAMBER = 1u << 2; // "bme280 chamber"
inline constexpr DefaultObjects HTU21D_DRYER = 1u << 3;   // "htu21d dryer"
inline constexpr DefaultObjects EBB_CAN_MCU = 1u << 4;    // "mcu EBBCan"
/// "temperature_sensor chamber" (the unconditional push).
inline constexpr DefaultObjects CHAMBER_SENSOR = 1u << 5;
inline constexpr DefaultObjects WIDTH_SENSOR = 1u << 6; // "hall_filament_width_sensor"
/// The default "filament_switch_sensor runout_sensor".
inline constexpr DefaultObjects RUNOUT_SENSOR = 1u << 7;
/// led_effect objects + LIGHTS_* / LED_* macros.
inline constexpr DefaultObjects LED_EFFECTS = 1u << 8;
} // namespace default_object

struct AxisMax {
    double x;
    double y;
    double z;
};

/// What a PrinterType presents to HelixScreen.
struct PersonaDescriptor {
    std::string_view hostname; // printer.info hostname
    /// stepper_* position_max == toolhead axis_maximum == mesh/grid bounds.
    AxisMax axis_max;
    std::string_view kinematics;       // configfile [printer] kinematics
    DefaultObjects omit;               // inherited defaults this persona does not have
    std::string_view default_mock_ams; // HELIX_MOCK_AMS when unset; "" = none chosen
    bool hardware_persona;             // production backend drives it (implies --real-ams)
    /// Probe profile (HELIX_MOCK_PROBE_TYPE value) reported in place of the
    /// default cartographer when the persona omits CARTOGRAPHER; "" = no probe.
    std::string_view probe = {};
};

[[nodiscard]] constexpr PersonaDescriptor descriptor(PrinterType type) {
    using namespace default_object;
    constexpr AxisMax standard{250.0, 250.0, 300.0};
    switch (type) {
    case PrinterType::CREALITY_K1:
        return {"k1c-mock", {229.0, 227.0, 255.0}, "corexy", NONE, "", false};
    case PrinterType::CREALITY_K1_MAX:
        return {"k1max-mock", {300.0, 307.5, 300.0}, "corexy", NONE, "", false};
    case PrinterType::FLASHFORGE_AD5M:
        return {"ad5m-mock", {220.0, 220.0, 220.0}, "corexy", NONE, "", false};
    case PrinterType::FLASHFORGE_CREATOR5:
        return {"mock-printer", standard, "corexy", NONE, "toolchanger", false};
    case PrinterType::ELEGOO_CC1:
        return {"cosmos",
                {256.0, 265.0, 258.0},
                "corexy",
                HAPPY_HARE_MMU | CARTOGRAPHER | BME280_CHAMBER | HTU21D_DRYER | EBB_CAN_MCU |
                    WIDTH_SENSOR | RUNOUT_SENSOR | LED_EFFECTS,
                "",
                false,
                "load_cell_probe"};
    case PrinterType::FLASHFORGE_AD5X:
        return {"ad5x-mock",
                {220.0, 220.0, 220.0},
                "corexy",
                HAPPY_HARE_MMU | CARTOGRAPHER | BME280_CHAMBER | HTU21D_DRYER | EBB_CAN_MCU |
                    WIDTH_SENSOR | RUNOUT_SENSOR | LED_EFFECTS,
                "ifs",
                false,
                "loadcell"};
    case PrinterType::CREALITY_K2_PLUS:
        // Chamber reads from "temperature_sensor chamber_temp", not the default one.
        return {"K2Plus-50C1",
                {352.5, 400.0, 360.0},
                "corexy",
                HAPPY_HARE_MMU | CARTOGRAPHER | BME280_CHAMBER | HTU21D_DRYER | EBB_CAN_MCU |
                    CHAMBER_SENSOR | WIDTH_SENSOR | RUNOUT_SENSOR | LED_EFFECTS,
                "cfs",
                false};
    case PrinterType::SNAPMAKER_U1:
        return {"snapmaker-u1",
                {270.0, 270.0, 400.0},
                "cartesian",
                HAPPY_HARE_MMU | CARTOGRAPHER | BME280_CHAMBER | HTU21D_DRYER | EBB_CAN_MCU |
                    CHAMBER_SENSOR | WIDTH_SENSOR | RUNOUT_SENSOR,
                "snapmaker",
                false};
    case PrinterType::FLASHFORGE_CREATOR5_ZMOD:
        return {"mock-printer", standard, "corexy", HAPPY_HARE_MMU, "", true};
    case PrinterType::VORON_24:
    case PrinterType::VORON_TRIDENT:
    case PrinterType::GENERIC_COREXY:
        return {"mock-printer", standard, "corexy", NONE, "", false};
    case PrinterType::DELTA:
        return {"mock-printer", standard, "delta", NONE, "", false};
    case PrinterType::GENERIC_BEDSLINGER:
    case PrinterType::MULTI_EXTRUDER:
        return {"mock-printer", standard, "cartesian", NONE, "", false};
    }
    return {"mock-printer", standard, "corexy", NONE, "", false};
}

/// True when `type` keeps the default object(s) `object` (its descriptor does not omit them).
[[nodiscard]] constexpr bool inherits_default(PrinterType type, DefaultObjects object) {
    return (descriptor(type).omit & object) == 0;
}

/// One HELIX_MOCK_PRINTER value.
struct PersonaEntry {
    std::string_view id; // HELIX_MOCK_PRINTER value
    PrinterType type;
    /// Logged as "[MoonrakerManager] Creating MOCK client (<display_name>, Nx speed)".
    std::string_view display_name;
    /// Printer type written before detection; "" = let detection decide.
    std::string_view saved_type;
};

/// Every HELIX_MOCK_PRINTER value. Row 0 is the default persona. One row per
/// line: scripts/screenshot.sh extracts the ids with sed.
// clang-format off
inline constexpr std::array<PersonaEntry, 15> PERSONAS = {{
    {"voron_24", PrinterType::VORON_24, "Voron 2.4", ""},
    {"voron_trident", PrinterType::VORON_TRIDENT, "Voron Trident", ""},
    {"k1", PrinterType::CREALITY_K1, "Creality K1", "Creality K1C"},
    {"k1max", PrinterType::CREALITY_K1_MAX, "Creality K1 Max", "Creality K1 Max"},
    {"ad5m", PrinterType::FLASHFORGE_AD5M, "Flashforge AD5M", ""},
    {"creator5", PrinterType::FLASHFORGE_CREATOR5, "FlashForge Creator 5 Pro", ""},
    {"creator5_zmod", PrinterType::FLASHFORGE_CREATOR5_ZMOD, "FlashForge Creator 5 Pro (Z-Mod)", ""},
    {"generic_corexy", PrinterType::GENERIC_COREXY, "Generic CoreXY", ""},
    {"generic_bedslinger", PrinterType::GENERIC_BEDSLINGER, "Generic Bedslinger", ""},
    {"multi_extruder", PrinterType::MULTI_EXTRUDER, "Multi-Extruder", ""},
    {"delta", PrinterType::DELTA, "Generic Delta", ""},
    {"snapmaker_u1", PrinterType::SNAPMAKER_U1, "Snapmaker U1", ""},
    {"cc1", PrinterType::ELEGOO_CC1, "Elegoo Centauri Carbon", ""},
    {"ad5x", PrinterType::FLASHFORGE_AD5X, "Flashforge AD5X (mock IFS)", ""},
    {"k2", PrinterType::CREALITY_K2_PLUS, "Creality K2 Plus", ""},
}};
// clang-format on

/// The persona whose id is exactly `id` (case-sensitive), or nullptr.
[[nodiscard]] inline const PersonaEntry* find_persona(std::string_view id) {
    for (const auto& p : PERSONAS) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

/// The persona a HELIX_MOCK_PRINTER value selects. Unset and "" select the
/// default persona and count as recognised; any other unknown value also falls
/// back to the default, with *recognised set false so the caller can warn.
[[nodiscard]] inline const PersonaEntry& resolve_persona(const char* env,
                                                         bool* recognised = nullptr) {
    const PersonaEntry* found = (env && env[0]) ? find_persona(env) : &PERSONAS[0];
    if (recognised) {
        *recognised = found != nullptr;
    }
    return found ? *found : PERSONAS[0];
}

/// Every persona id, comma-separated in table order.
[[nodiscard]] inline std::string persona_ids() {
    std::string out;
    for (const auto& p : PERSONAS) {
        if (!out.empty()) {
            out += ", ";
        }
        out += p.id;
    }
    return out;
}

/// The mock AMS mode in effect: an explicit non-empty HELIX_MOCK_AMS
/// (lowercased) always wins; otherwise the HELIX_MOCK_PRINTER persona's
/// default; otherwise "". Every HELIX_MOCK_AMS reader goes through this.
[[nodiscard]] inline std::string effective_mock_ams(const char* ams_env, const char* printer_env) {
    if (ams_env && ams_env[0]) {
        return helix::text_io::to_lower(ams_env);
    }
    if (printer_env) {
        if (const PersonaEntry* p = find_persona(printer_env)) {
            return std::string(descriptor(p->type).default_mock_ams);
        }
    }
    return {};
}

/// True when a HELIX_MOCK_PRINTER value names a mock HARDWARE persona: the
/// persona publishes the Klipper objects and status a real machine runs, and a
/// production backend is meant to drive them rather than a mock backend. The
/// CLI's --real-ams implication and AmsBackend's mock selection both
/// route through this rule. Exact and case-sensitive, like the persona
/// selection itself.
[[nodiscard]] inline bool is_hardware_persona(std::string_view persona) {
    const PersonaEntry* p = find_persona(persona);
    return p && descriptor(p->type).hardware_persona;
}

} // namespace helix::mock
