// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/diag_upload_gate.h"

#include <cstdlib>
#include <cstring>

namespace helix::diag {

bool uploads_enabled() {
    // Compile default: official packaging builds only (prestonbrown/helixscreen#1410).
    const bool default_on = marked_build();

    const char* val = std::getenv("HELIX_DIAGNOSTIC_UPLOADS");
    if (val != nullptr && std::strcmp(val, "0") == 0) {
        return false;
    }
    if (val != nullptr && std::strcmp(val, "1") == 0) {
        return true;
    }
    return default_on;
}

} // namespace helix::diag
