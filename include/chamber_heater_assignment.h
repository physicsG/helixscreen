// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file chamber_heater_assignment.h
 * @brief Which chamber heater and chamber sensor a printer has, given its chamber
 *        assignments
 */

#pragma once

#include <string>

namespace helix {
class PrinterCapabilitiesState;
class PrinterDiscovery;
class PrinterTemperatureState;
class TemperatureController;
} // namespace helix

namespace helix::chamber {

/**
 * @brief The chamber heater this printer has under a chamber-heater assignment.
 *
 * The single answer to "does this printer have a chamber heater, and which object
 * is it". PrinterState publishes the result as the resolved chamber heater name and
 * the printer_has_chamber_heater capability; every other consumer reads what it
 * published rather than discovery's own pick.
 *
 * A named heater counts only while Klipper reports that object. Presets seed a
 * model family's heater name before the wizard runs, so a family member without
 * the heater carries an assignment for hardware it lacks; that name falls back to
 * discovery's pick and can never make a chamber heater appear on its own.
 *
 * @param assignment "auto" takes discovery's pick, "none" disables the chamber
 *                   heater, any other value names a Klipper object
 * @param discovery  The printer's discovered hardware
 * @return Full Klipper object name, or empty when the printer has no chamber heater
 */
std::string resolve_heater(const std::string& assignment, const PrinterDiscovery& discovery);

/**
 * @brief The chamber temperature sensor this printer has under a chamber-sensor
 *        assignment.
 *
 * resolve_heater()'s rule against discovery's chamber sensor. PrinterState publishes
 * the result as the resolved chamber sensor name and the printer_has_chamber_sensor
 * capability; every other consumer reads what it published rather than discovery's
 * own pick, except the sensor settings dropdown, whose "Auto" entry names the sensor
 * auto takes.
 *
 * A named sensor counts only while Klipper reports that object. A saved name the
 * printer's configuration no longer has, such as one a model preset seeded, falls
 * back to discovery's pick, so the chamber reads the sensor the printer does have.
 *
 * @param assignment "auto" takes discovery's pick, "none" disables the chamber
 *                   sensor, any other value names a Klipper object
 * @param discovery  The printer's discovered hardware
 * @return Full Klipper object name, or empty when the printer has no chamber sensor
 */
std::string resolve_sensor(const std::string& assignment, const PrinterDiscovery& discovery);

/**
 * @brief Resolve the chamber sensor and heater and publish them to their domains
 *
 * Sets the resolved names, the cooling fan and its resting target on @p temps;
 * attaches the matched backend's diagnostics source, actions and dryer (to
 * @p tc when there is one) only while the resolved heater is discovery's own
 * pick, so a manual override to another heater or "none" detaches them; sets
 * the chamber capability flags on @p caps; and promotes the resolved sensor to
 * the CHAMBER role in the temperature sensor manager. Main thread only.
 */
void apply_resolution(const PrinterDiscovery& discovery, const std::string& sensor_assignment,
                      const std::string& heater_assignment, PrinterTemperatureState& temps,
                      PrinterCapabilitiesState& caps, TemperatureController* tc);

} // namespace helix::chamber
