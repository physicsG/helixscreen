// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ams_types.h"

#include <string>

#include "hv/json.hpp"

namespace helix::printer {

/// One decoded entry from the `!! {"code":"keyNNN","msg":...}` error channel.
struct KlipperErrorEntry {
    const char* message;
    const char* hint;
    AmsAlertLevel level;
    /// Optional formatter that stringifies the `values` array into a
    /// human-readable locator (" in unit 1 slot B"). Caller appends the
    /// result to the friendly message. nullptr = no per-error format
    /// known yet (no regression — message displays unchanged).
    std::string (*format_values)(const nlohmann::json&) = nullptr;
    /// True when the firmware's own `msg` names the real cause and the table
    /// text cannot (one code, several causes: key843 fires for a busy box AND
    /// an unreadable tag). A caller with a firmware msg then prefers its
    /// wording as the message (#1387).
    bool prefer_fw_msg = false;
};

/// Look up a `keyNNN` code emitted on Klipper's gcode-error channel.
/// Returns nullptr for an unknown code.
///
/// The table lives here rather than inside a backend because the codes arrive
/// on a channel every printer has, and the consumer is the application-layer
/// gcode-error router — not the filament system. Three of the codes (key111
/// pre-heat, key298 MCU bridge, key585 out of range) are Klipper-layer faults
/// a machine reports with no filament hardware attached at all, so a build
/// without CFS compiled in must still be able to translate them.
const KlipperErrorEntry* klipper_error_lookup(const std::string& key_code);

} // namespace helix::printer
