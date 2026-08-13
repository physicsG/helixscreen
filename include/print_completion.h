// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_observer_guard.h"

// Forward declarations
class IMoonrakerAPI;

namespace helix {

/**
 * @brief Initialize print completion notification system
 *
 * Registers an observer on PrinterState's print_state_enum subject that
 * triggers toast or modal notifications when prints complete, are cancelled,
 * or fail. Uses SettingsManager to determine notification mode.
 *
 * @return ObserverGuard that manages the observer's lifetime
 */
ObserverGuard init_print_completion_observer();

/**
 * @brief Clean up stale .helix_temp files on startup
 *
 * Deletes all files in the .helix_temp directory on Moonraker.
 * These are temp files created when modifying G-code for prints.
 * Should be called after Moonraker connection is established.
 *
 * @param api IMoonrakerAPI instance to use for file operations
 */
void cleanup_stale_helix_temp_files(IMoonrakerAPI* api);

} // namespace helix
