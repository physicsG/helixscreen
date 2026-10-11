// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filament_macro_profiles.h"

#include "text_io.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace helix::filament_macros {
namespace {

/// One stock macro, and what it does for itself.
struct MacroProfile {
    /// Uppercase macro name. Must be a spelling that carries no meaning in
    /// stock Klipper - a generic name proves nothing about the body.
    const char* name;
    /// Which firmware ships it, for the reader deciding whether a new row
    /// belongs beside this one.
    const char* firmware;
    /// Drives the hotend to temperature and waits, so a UI preheat in front of
    /// it is a second wait at a temperature the macro then overrides.
    bool heats_hotend;
    /// Carries its own conditional home, so a "home first?" prompt and any
    /// synthesized G28 are redundant.
    bool homes_if_needed;
};

// QIDI's stock screen drives M604 to load and M603 to unload. Both take the
// temperature as `params.S` (default 250) and drive it themselves with
// `M104 S{hotendtemp}` followed by a blocking `M109 S{hotendtemp}`, so the
// hotend is already at temperature by the time either extrudes - a preheat in
// front of one waits for the same heat twice, at whatever temperature we picked
// rather than the one the macro then commands. AmsBackendQidi drives
// `M603 S<temp>` for the same reason.
//
// Neither spelling means anything in Marlin either (M603 there configures a
// filament change and there is no M604), so a Klipper config defining them is
// running QIDI's convention.
//
// Both also open with `_CG28`, QIDI's conditional home
// (`{% if "xyz" not in printer.toolhead.homed_axes %} G28 {% endif %}`), so on
// the macro tier neither needs a home asked for in front of it.
//
// That is a claim about these macros run ALONE. AmsBackendQidi composes them
// with CLEAR_NOZZLE, which has no `_CG28` and reaches Z through MOVE_TO_TRASH's
// XY-only guard - which is why the backend still answers
// delegates_homing_to_printer() false and routes through ensure_homed_then().
constexpr std::array<MacroProfile, 2> MACRO_PROFILES{{
    {"M604", "QIDI stock", /*heats_hotend=*/true, /*homes_if_needed=*/true},
    {"M603", "QIDI stock", /*heats_hotend=*/true, /*homes_if_needed=*/true},
}};

/// The row for @p macro_name, or nullptr when no firmware we know ships it.
const MacroProfile* find_profile(const std::string& macro_name) {
    if (macro_name.empty()) {
        return nullptr;
    }
    const std::string upper = helix::text_io::to_upper(macro_name);
    for (const MacroProfile& p : MACRO_PROFILES) {
        if (upper == p.name) {
            return &p;
        }
    }
    return nullptr;
}

} // namespace

bool macro_heats_hotend(const std::string& macro_name) {
    const MacroProfile* p = find_profile(macro_name);
    return p && p->heats_hotend;
}

bool macro_homes_if_needed(const std::string& macro_name) {
    const MacroProfile* p = find_profile(macro_name);
    return p && p->homes_if_needed;
}

} // namespace helix::filament_macros
