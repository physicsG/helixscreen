// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "calibration_types.h"

#include <string>

/**
 * @file screws_tilt_parser.h
 * @brief Parser for Klipper SCREWS_TILT_CALCULATE console output
 *
 * Split out of ScrewsTiltCollector so the same parser serves the live
 * WebSocket collector, the mock printer, and unit tests. Anything that turns
 * Klipper text into a ScrewTiltResult must go through here — a second copy is
 * how the mock ended up emitting the opposite sign convention from Klipper
 * (prestonbrown/helixscreen#1225).
 */

namespace helix {

/**
 * @brief Parse one "// screw_name : x=… , y=… , z=… : adjust CW TT:MM" line
 *
 * Also handles the base screw's "// screw_name (base) : x=…" form. The returned
 * adjustment is Klipper's own text, unmodified.
 *
 * @param line Raw notify_gcode_response line, including its "//" prefix
 * @param out  Filled in only when the line is a screw result
 * @return true if the line was a screw result line
 */
bool parse_screws_tilt_line(const std::string& line, ScrewTiltResult& out);

} // namespace helix
