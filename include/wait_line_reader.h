// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gcode_wait_line.h"
#include "json_fwd.h"
#include "moonraker_error.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace helix {

class ITransfersAPI;

/// virtual_sdcard.file_position from a printer.objects.query response or a
/// notify_status_update status object; nullopt when it does not carry it.
std::optional<uint64_t> virtual_sdcard_file_position(const json& query_response);

/// Read the command the print is executing: the line starting at
/// @p file_position in gcodes/@p filename, classified. Call on the main
/// thread; @p on_line and @p on_error run on the transfer's callback thread.
void read_wait_line_at(ITransfersAPI& transfers, const std::string& filename,
                       uint64_t file_position, std::function<void(const WaitLine&)> on_line,
                       std::function<void(const MoonrakerError&)> on_error);

} // namespace helix
