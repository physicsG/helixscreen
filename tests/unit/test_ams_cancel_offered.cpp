// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ams_cancel_offered.cpp
 * @brief A backend whose cancel() can never act offers no Cancel or Abort.
 *
 * The sidebar's Cancel and Device Operations' Abort both ask
 * can_cancel_operation(); a backend answering true there with a cancel() that
 * always refuses shows the user a button that only fails.
 */

#include "ams_backend_multiace.h"
#include "ams_backend_qidi.h"
#include "ams_backend_snapmaker.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

TEST_CASE("Backends whose cancel() cannot act offer no Cancel", "[ams][cancel]") {
    SECTION("Snapmaker U1") {
        AmsBackendSnapmaker backend(nullptr, nullptr);
        REQUIRE(backend.cancel().result == AmsResult::NOT_SUPPORTED);
        CHECK_FALSE(backend.can_cancel_operation());
    }
    SECTION("Multi-ACE, which cancels as the U1 does") {
        AmsBackendMultiAce backend(nullptr, nullptr);
        REQUIRE(backend.cancel().result == AmsResult::NOT_SUPPORTED);
        CHECK_FALSE(backend.can_cancel_operation());
    }
    SECTION("QIDI Box") {
        AmsBackendQidi backend(nullptr, nullptr);
        REQUIRE(backend.cancel().result == AmsResult::NOT_SUPPORTED);
        CHECK_FALSE(backend.can_cancel_operation());
    }
}
