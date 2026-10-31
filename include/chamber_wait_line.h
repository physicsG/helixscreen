// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gcode_wait_line.h"

#include <string>

namespace helix {

/// The line is a TEMPERATURE_WAIT on the chamber heater itself: its SENSOR
/// is @p chamber_heater's object name, compared case-insensitively. A wait on
/// any other sensor is not, including a separate chamber sensor: with the
/// heater at 0 that is a passive heat soak, not a heater that failed to start.
bool wait_names_chamber_heater(const WaitLine& line, const std::string& chamber_heater);

/// The chamber temperature @p line waits for: M191's target or a
/// TEMPERATURE_WAIT's MINIMUM; 0 = none.
double chamber_wait_target_c(const WaitLine& line);

} // namespace helix
