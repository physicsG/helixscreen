// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app_globals.h"

/// Installs a caller-owned API as the process-global Moonraker API for a scope
/// and restores the previous one. Same contract as ScopedMoonrakerClient: the
/// API stays caller-owned and must outlive the guard.
class ScopedMoonrakerApi {
  public:
    explicit ScopedMoonrakerApi(helix::IMoonrakerAPI* api) : previous_(get_moonraker_api()) {
        set_moonraker_api(api);
    }
    ~ScopedMoonrakerApi() {
        set_moonraker_api(previous_);
    }

    ScopedMoonrakerApi(const ScopedMoonrakerApi&) = delete;
    ScopedMoonrakerApi& operator=(const ScopedMoonrakerApi&) = delete;
    ScopedMoonrakerApi(ScopedMoonrakerApi&&) = delete;
    ScopedMoonrakerApi& operator=(ScopedMoonrakerApi&&) = delete;

  private:
    helix::IMoonrakerAPI* previous_;
};
