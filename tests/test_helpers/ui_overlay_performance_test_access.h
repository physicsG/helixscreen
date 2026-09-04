// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_overlay_performance.h"

namespace helix::ui {

class UiOverlayPerformanceTestAccess {
  public:
    /// Clear cached root/card pointers and observer so create() can be called
    /// again after the LVGL display has been reinitialized.
    static void reset(UiOverlayPerformance& o) {
        o.mcu_names_observer_.reset();
        o.root_ = nullptr;
        o.mcu_card_ = nullptr;
        o.last_mcu_names_.clear();
    }
};

} // namespace helix::ui
