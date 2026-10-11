// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "filament_slot_override_store.h"

#include <chrono>
#include <filesystem>
#include <utility>

// Friend of FilamentSlotOverrideStore: reaches its private tunables.
class FilamentSlotOverrideStoreTestAccess {
  public:
    static void set_load_timeout(helix::ams::FilamentSlotOverrideStore& store,
                                 std::chrono::milliseconds ms) {
        store.load_timeout_ = ms;
    }
    // Redirect the read-cache to a per-test tmp dir so tests never touch the
    // user's real config. Empty path restores the default (get_user_config_dir).
    static void set_cache_directory(helix::ams::FilamentSlotOverrideStore& store,
                                    std::filesystem::path dir) {
        store.cache_dir_ = std::move(dir);
    }
};
