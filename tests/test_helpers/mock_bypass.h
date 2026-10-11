// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ams_backend_mock.h"

namespace helix::test {

/// Empty the toolhead of a started mock so enable_bypass() is accepted. The mock
/// boots with a lane loaded, and its default Happy Hare persona refuses bypass
/// with filament at the toolhead, as the hardware does. Returns false when the
/// unload was refused.
inline bool unload_for_bypass(AmsBackendMock& mock) {
    mock.set_operation_delay(0);
    if (!mock.unload_active_filament().success()) {
        return false;
    }
    mock.wait_for_operation_thread();
    return !mock.get_system_info().filament_loaded;
}

} // namespace helix::test
