// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "hv/json.hpp"

namespace helix {
class PrinterDiscovery;
} // namespace helix

/**
 * @file power_loss_sensor.h
 * @brief Firmwares that monitor incoming mains, and how to read them.
 *
 * power_loss_check is a LIVE voltage monitor, not a record of a past loss: the
 * firmware shuts Klipper down while mains is dropping and restarts it once
 * mains is back, so by the time HelixScreen is running again the flag reads 0.
 * It therefore answers "how does mains look right now" (with voltage_type and
 * duty_percent beside it) and gates nothing — power-loss recovery availability
 * stays what the PLR backend already validates against MCU flash on boot.
 *
 * This is the one module that knows which firmwares publish such a monitor and
 * where it keeps its fields. Generic code asks the capability questions below
 * and never names a firmware. Adding another is a row in the provider table.
 */
namespace helix::power_loss {

/// True when this firmware publishes an incoming-mains monitor.
[[nodiscard]] bool firmware_reports_power_loss(const PrinterDiscovery& hw);

/// Status objects carrying the monitor's fields, for the subscription builder.
/// Empty when the firmware has none.
[[nodiscard]] std::vector<std::string> required_status_objects(const PrinterDiscovery& hw);

/// Whether the monitor currently reports a power loss, read out of a Moonraker
/// status frame.
///
/// nullopt is "this frame says nothing" — the monitor absent from a delta
/// frame, `initialized` 0 (the monitor has taken no reading), or a field
/// arriving as anything but a number. None of those may be read as "mains is
/// fine": absence of evidence is not the evidence a recovery decision would
/// need, which is one reason nothing recovery-shaped asks this question.
[[nodiscard]] std::optional<bool> power_loss_asserted(const PrinterDiscovery& hw,
                                                      const nlohmann::json& status);

} // namespace helix::power_loss
