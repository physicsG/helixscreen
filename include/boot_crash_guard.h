// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "config.h"

#include <algorithm>
#include <string>
#include <vector>

namespace helix {

/// Crash resets in a row, with no healthy session between them, that make boot stop
/// connecting to the active printer (prestonbrown/helixscreen#1750).
constexpr int BOOT_CRASH_FALLBACK_THRESHOLD = 3;

/// Config key: boot does not connect to the active printer until the user picks one.
inline constexpr const char* BOOT_CONNECT_HOLD_KEY = "/boot_connect_hold";
/// Config key: crash resets since the last healthy session.
inline constexpr const char* BOOT_CRASH_STREAK_KEY = "/boot_crash_streak";
/// Config key: the printer a switch left, where a crash run on the new one falls back to.
inline constexpr const char* SWITCH_PREVIOUS_PRINTER_KEY = "/switch_previous_printer_id";
/// Config key: restart fallbacks since the last healthy session.
inline constexpr const char* SWITCH_RESTART_STREAK_KEY = "/switch_restart_streak";

struct BootPrinterChoice {
    int crash_streak = 0;     ///< The streak to persist for the next boot.
    std::string fallback_id;  ///< Printer to make active instead; empty keeps the active one.
    bool auto_connect = true; ///< False holds the connection; persist it as the hold.
    bool tripped = false;     ///< This boot reached the threshold.
};

/// Which printer a boot connects to. A crash reset adds one to the streak; a deliberate
/// restart or a power-on leaves it alone, and a healthy session clears it elsewhere. At the
/// threshold the boot falls back to the printer that was active before the last switch, or,
/// with none to go back to, holds the connection. A held connection stays held across every
/// later boot until a user pick clears it. The streak restarts from zero at the threshold, so
/// the panel falls back again only after another full run of crashes.
inline BootPrinterChoice choose_boot_printer(bool crash_reset, int crash_streak, bool connect_held,
                                             const std::string& active_id,
                                             const std::string& previous_id,
                                             const std::vector<std::string>& printer_ids) {
    if (!crash_reset) {
        return {crash_streak, {}, !connect_held, false};
    }
    if (crash_streak + 1 < BOOT_CRASH_FALLBACK_THRESHOLD) {
        return {crash_streak + 1, {}, !connect_held, false};
    }
    const bool previous_exists =
        std::find(printer_ids.begin(), printer_ids.end(), previous_id) != printer_ids.end();
    if (!previous_id.empty() && previous_id != active_id && previous_exists) {
        return {0, previous_id, true, true};
    }
    return {0, {}, false, true};
}

/// A session stayed connected long enough to count as healthy: no crash or restart before it
/// is part of a loop, and a connection made outside a pick (Change Host) ends the hold.
/// Returns whether anything changed, so the caller saves only then.
inline bool end_boot_crash_run(Config& config) {
    bool changed = false;
    for (const char* key : {BOOT_CRASH_STREAK_KEY, SWITCH_RESTART_STREAK_KEY}) {
        if (config.get<int>(key, 0) != 0) {
            config.set<int>(key, 0);
            changed = true;
        }
    }
    if (config.get<bool>(BOOT_CONNECT_HOLD_KEY, false)) {
        config.set<bool>(BOOT_CONNECT_HOLD_KEY, false);
        changed = true;
    }
    return changed;
}

} // namespace helix
