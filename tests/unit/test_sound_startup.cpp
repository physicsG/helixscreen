// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sound_manager.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("The startup chime plays on a normal run and not under --test", "[sound][startup]") {
    CHECK(helix::SoundManager::plays_startup_sound(false));
    CHECK_FALSE(helix::SoundManager::plays_startup_sound(true));
}
