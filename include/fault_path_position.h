// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ams_types.h"

#include <optional>
#include <string>
#include <string_view>

/**
 * @file fault_path_position.h
 * @brief Firmwares whose fault messages say where along the filament path a
 *        load stopped.
 *
 * Some filament firmware names the stop point in the fault sentence and welds a
 * text diagram of it onto the message. This is the one module that knows which
 * firmwares do that and how to read them. The fault-path graphic and the modals
 * that embed it ask the question and never name a firmware. Adding another is
 * a row in the provider table.
 *
 * Pure functions over strings: no LVGL, no printer state, no locking.
 */
namespace helix::faultpath {

/// Where the filament had reached when the fault was raised, or nullopt when
/// no provider recognises @p message.
[[nodiscard]] std::optional<PathSegment> fault_position(std::string_view message);

/// @p message with the recognising provider's text diagram removed, unchanged
/// when no provider recognises it.
[[nodiscard]] std::string strip_position_diagram(std::string_view message);

} // namespace helix::faultpath
