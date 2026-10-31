// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Firmware that prints a .3mf natively by extracting the printing plate's
// G-code into a Moonraker root of its own. Generic code asks where such an
// extract lives and whether a file is one; which firmware writes which names
// is this module's table alone.

#include <string>

namespace helix::firmware_3mf {

/// The Moonraker root firmware extracts a printing .3mf's plate into.
const char* extract_root();

/**
 * @brief Whether @p entry, a file in extract_root(), is a firmware extract of
 *        the printing .3mf @p print_filename.
 *
 * True for the firmware's plate-numbered extract names (no plate index ties
 * them to one print) and for the extract named after @p print_filename itself
 * (extract_name(), compared case-insensitively).
 */
bool is_extract(const std::string& entry, const std::string& print_filename);

/// Whether @p name, a bare filename in extract_root(), is a plate-numbered
/// extract ("<prefix><plate>.gcode", the plate non-empty; case-sensitive, as
/// the firmware writes it).
bool is_plate_extract(const std::string& name);

/// The extract named after @p print_filename: its basename without ".3mf",
/// with ".gcode" added when that leaves none ("Benchy.gcode.3mf" and
/// "Benchy.3mf" both give "Benchy.gcode").
std::string extract_name(const std::string& print_filename);

} // namespace helix::firmware_3mf
