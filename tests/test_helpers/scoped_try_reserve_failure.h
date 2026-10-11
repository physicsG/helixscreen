// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "try_reserve.h"

namespace helix {

/// Makes every try_reserve() that would allocate fail for this object's
/// lifetime, and restores it even when an assertion unwinds the test.
class ScopedTryReserveFailure {
  public:
    ScopedTryReserveFailure() {
        try_reserve_fails_for_test().store(true);
    }
    ~ScopedTryReserveFailure() {
        try_reserve_fails_for_test().store(false);
    }
    ScopedTryReserveFailure(const ScopedTryReserveFailure&) = delete;
    ScopedTryReserveFailure& operator=(const ScopedTryReserveFailure&) = delete;
};

} // namespace helix
