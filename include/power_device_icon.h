// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstring>

namespace helix {

/// Icons with distinct on/off glyphs. Config always stores the ON variant;
/// resolve_icon_for_state() derives the OFF variant from this table.
struct PowerIconPair {
    const char* on_icon;
    const char* off_icon;
};
inline const PowerIconPair POWER_ICON_PAIRS[] = {
    {"power_on", "power_off"},
    {"power_plug", "power_plug_off"},
    {"lightbulb_on", "lightbulb_outline"},
    {"fan", "fan_off"},
};

/// Map an off-variant icon name to its on-variant (e.g., "fan_off" -> "fan").
/// Returns the input unchanged if it's not an off-variant.
inline const char* power_icon_to_on_variant(const char* icon) {
    for (const auto& pair : POWER_ICON_PAIRS) {
        if (std::strcmp(icon, pair.off_icon) == 0)
            return pair.on_icon;
    }
    return icon;
}

/// Return the icon to display for a given power status (0=off, 1=on, 2=locked).
///
/// A negative status means Moonraker never reported the device (it is
/// unconfigured), and the tile speaks to that with its "Configure" label:
/// the slash glyph of an off-variant would claim the device is switched off,
/// which nobody told us. Off (0) and locked (2) swap a paired icon to its
/// off-variant; on (1) keeps the configured base icon.
inline const char* power_resolve_icon_for_state(const char* base_icon, int status) {
    if (status < 0 || status == 1)
        return base_icon;
    for (const auto& pair : POWER_ICON_PAIRS) {
        if (std::strcmp(base_icon, pair.on_icon) == 0)
            return pair.off_icon;
    }
    return base_icon;
}

} // namespace helix
