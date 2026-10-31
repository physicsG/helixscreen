// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace helix {
class PrinterDiscovery;
} // namespace helix

/**
 * @file power_loss_sensor.h
 * @brief Which status objects carry a firmware's incoming-mains monitor.
 *
 * power_loss_check is a LIVE voltage monitor, not a record of a past loss: the
 * firmware shuts Klipper down while mains is dropping and restarts it once
 * mains is back, so by the time HelixScreen is running again the flag reads 0.
 * It gates nothing: power-loss recovery availability stays what the PLR backend
 * already validates against MCU flash on boot.
 *
 * The object names are rows in the object table (kind PowerLossMonitor); the
 * table matches the bare name only, so the per-extruder `power_loss_check e0`
 * objects never count.
 */
namespace helix::power_loss {

/// Status objects carrying the monitor's fields, for the subscription builder.
/// Empty when the firmware has none.
[[nodiscard]] std::vector<std::string> required_status_objects(const PrinterDiscovery& hw);

} // namespace helix::power_loss
