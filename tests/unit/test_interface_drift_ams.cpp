// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Compile-only drift protection for AmsBackend.

#include "ams_backend.h"

#include "../catch_amalgamated.hpp"

#ifdef HELIX_ENABLE_MOCKS
#include "ams_backend_mock.h"

TEST_CASE("AmsBackendMock satisfies AmsBackend interface", "[compile][drift]") {
    std::unique_ptr<helix::AmsBackend> p = std::make_unique<helix::AmsBackendMock>();
    REQUIRE(p != nullptr);
}
#endif
