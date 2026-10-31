// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace helix {

/// CRC-32 (IEEE 802.3, the polynomial PNG chunks and ZIP entries use), fed in
/// pieces: start from 0 and pass each result back in with the next bytes.
uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t size);

} // namespace helix
