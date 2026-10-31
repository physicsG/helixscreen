// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ams_types.h"

#include <string_view>
#include <utility>
#include <vector>

/**
 * @file hh_defaults.h
 * @brief Default device sections and actions for Happy Hare backends
 *
 * Provides the canonical section/action definitions for Happy Hare MMU systems.
 * Used by both the real backend (as a starting point for dynamic overlay) and
 * the mock backend (directly).
 *
 * Shared section IDs with AFC: "setup", "speed", "maintenance"
 */
namespace helix::printer {

/**
 * @brief Default device sections for Happy Hare
 *
 * Returns 5 sections: setup, speed, toolhead, accessories, maintenance.
 * Section IDs match AFC convention for UI consistency.
 */
std::vector<DeviceSection> hh_default_sections();

/**
 * @brief Default device actions for Happy Hare
 *
 * Returns core essential actions for each section.
 * Real backend overlays dynamic values from MMU state.
 */
std::vector<DeviceAction> hh_default_actions();

/// The {min, max} of default action @p id, or a range wide enough to accept any value.
std::pair<float, float> hh_default_action_range(std::string_view id);

} // namespace helix::printer
