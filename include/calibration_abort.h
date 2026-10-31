// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace helix {
class IMoonrakerAPI;

/// Stop a calibration that cannot be cancelled cleanly: M112, then a firmware
/// restart once the stop is acknowledged. Fire-and-forget; failures are logged.
void emergency_stop_and_restart(IMoonrakerAPI* api, const char* log_tag);

} // namespace helix
