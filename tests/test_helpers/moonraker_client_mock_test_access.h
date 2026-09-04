// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "moonraker_client_mock.h"

namespace helix {

// Grants tests access to MoonrakerClientMock internals that have no public
// equivalent. Declared a friend of MoonrakerClientMock (see
// moonraker_client_mock.h). Follows the existing TestAccess pattern
// (tests/test_helpers/, cf. MoonrakerClientTestAccess) rather than adding
// production _for_testing() accessors.
class MoonrakerClientMockTestAccess {
  public:
    // The cached status key the mock uses for chamber-heater frames
    // (e.g. "heater_generic dragonbreath"). Reading it directly pins the
    // registry-consulting cache without guessing from dispatched frames.
    static std::string chamber_heater_status_key(const MoonrakerClientMock& c) {
        return c.chamber_heater_status_key();
    }

    // Synchronously dispatch the initial subscription state to registered
    // notify callbacks — the same frames connect() emits, without the
    // 250 ms connect delay, historical-temperature burst, and simulation
    // thread that make connect()-based assertions racy.
    static void dispatch_initial_state(MoonrakerClientMock& c) {
        c.dispatch_initial_state();
    }
};

} // namespace helix
