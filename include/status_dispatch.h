// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "printer_state.h"

#include <cstdint>
#include <optional>

namespace helix {

/// Hand one status frame to every consumer, on the main thread: PrinterState and
/// its domains, ToolState, the LED controller's strip-colour cache and the sensor
/// managers. @p klippy_epoch is the epoch the frame was received under.
void dispatch_status_frame(const StatusFrame& frame, std::optional<uint64_t> klippy_epoch);

/// The same hand-off for a status object that did not arrive as a notification
/// (a printer.objects.query result): untimestamped, current, no epoch. Main thread only.
void dispatch_status(const json& status);

} // namespace helix
