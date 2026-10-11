// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// No dependencies: Config's migrations use this, and Config also builds into
// the splash binary, which has no LVGL.

namespace helix {

/** @brief Print completion notification mode (Off=0, Notification=1, Alert=2) */
enum class CompletionAlertMode { OFF = 0, NOTIFICATION = 1, ALERT = 2 };

} // namespace helix
