// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_power_device_icon.cpp
 * @brief The power tile's glyph matches what Moonraker actually reported:
 *        unconfigured stays the configured icon, off swaps to the paired
 *        off-variant, on keeps it.
 *
 * Run with: ./build/bin/helix-tests "[power_device_icon]"
 */

#include "power_device_icon.h"

#include <cstring>

#include "../catch_amalgamated.hpp"

TEST_CASE("power tile icon resolves by reported status", "[power_device_icon]") {
    // A paired icon, so the swap branch is reachable for these cases.
    REQUIRE(std::strcmp(helix::power_resolve_icon_for_state("fan", 0), "fan_off") == 0);

    SECTION("unconfigured (negative status) keeps the base icon") {
        CHECK(std::strcmp(helix::power_resolve_icon_for_state("fan", -1), "fan") == 0);
    }
    SECTION("off (0) draws the off-variant") {
        CHECK(std::strcmp(helix::power_resolve_icon_for_state("fan", 0), "fan_off") == 0);
    }
    SECTION("on (1) keeps the base icon") {
        CHECK(std::strcmp(helix::power_resolve_icon_for_state("fan", 1), "fan") == 0);
    }
    SECTION("locked (2) still draws the off-variant") {
        CHECK(std::strcmp(helix::power_resolve_icon_for_state("fan", 2), "fan_off") == 0);
    }
    SECTION("an unpaired icon never changes") {
        CHECK(std::strcmp(helix::power_resolve_icon_for_state("power_cycle", 0), "power_cycle") ==
              0);
    }
}

TEST_CASE("power tile icon picker stores the on variant", "[power_device_icon]") {
    CHECK(std::strcmp(helix::power_icon_to_on_variant("fan_off"), "fan") == 0);
    CHECK(std::strcmp(helix::power_icon_to_on_variant("fan"), "fan") == 0);
    CHECK(std::strcmp(helix::power_icon_to_on_variant("power_cycle"), "power_cycle") == 0);
}
