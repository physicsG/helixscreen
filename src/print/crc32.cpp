// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "crc32.h"

namespace helix {

uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t size) {
    static const auto table = [] {
        struct Table {
            uint32_t v[256];
        } t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            t.v[n] = c;
        }
        return t;
    }();
    uint32_t c = crc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        c = table.v[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

} // namespace helix
