// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>

namespace helix {

/// Fill @p out with random bytes from /dev/urandom, or std::random_device when that
/// is unavailable.
void fill_random_bytes(unsigned char* out, size_t count);

} // namespace helix
