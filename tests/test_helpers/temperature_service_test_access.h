// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "temperature_service.h"

struct TemperatureServiceTestAccess {
    /// The filament panel's mini graph; null until setup_mini_combined_graph() builds it.
    static const helix::TempGraphController* mini_graph(const TemperatureService& service) {
        return service.mini_graph_controller_.get();
    }
};
