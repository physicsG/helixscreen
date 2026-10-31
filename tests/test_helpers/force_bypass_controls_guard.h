// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "settings_manager.h"

namespace helix::test {

/// Turns the "force bypass controls" override on for one scope. SettingsManager
/// holds no value until its subjects exist, so the guard initializes them when
/// a fixture has not, and leaves them as it found them. Needs LVGL up
/// (LVGLTestFixture or a descendant).
class ForceBypassControlsGuard {
  public:
    ForceBypassControlsGuard() {
        auto& sm = SettingsManager::instance();
        if (!sm.are_subjects_initialized()) {
            sm.init_subjects();
            initialized_here_ = true;
        }
        sm.set_ams_force_bypass_controls(true);
    }

    ~ForceBypassControlsGuard() {
        auto& sm = SettingsManager::instance();
        sm.set_ams_force_bypass_controls(false);
        if (initialized_here_) {
            sm.deinit_subjects();
        }
    }

    ForceBypassControlsGuard(const ForceBypassControlsGuard&) = delete;
    ForceBypassControlsGuard& operator=(const ForceBypassControlsGuard&) = delete;

  private:
    bool initialized_here_ = false;
};

} // namespace helix::test
