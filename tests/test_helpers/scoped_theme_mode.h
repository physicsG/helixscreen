// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "theme_manager.h"

/// Switches the active theme between light and dark for one test and puts back
/// whichever mode was active, even when a REQUIRE abandons the case.
class ScopedThemeMode {
  public:
    ScopedThemeMode() : was_dark_(theme_manager_is_dark_mode()) {}
    ~ScopedThemeMode() {
        theme_manager_apply_theme(theme_manager_get_active_theme(), was_dark_);
    }
    ScopedThemeMode(const ScopedThemeMode&) = delete;
    ScopedThemeMode& operator=(const ScopedThemeMode&) = delete;

    void set(bool dark) {
        theme_manager_apply_theme(theme_manager_get_active_theme(), dark);
    }

  private:
    bool was_dark_;
};
