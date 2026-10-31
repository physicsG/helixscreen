// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "random_bytes.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <fstream>
#include <random>

namespace helix {

void fill_random_bytes(unsigned char* out, size_t count) {
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    size_t filled = 0;
    while (filled < count && urandom.good()) {
        urandom.read(reinterpret_cast<char*>(out + filled),
                     static_cast<std::streamsize>(count - filled));
        filled += static_cast<size_t>(urandom.gcount());
    }
    if (filled == count) {
        return;
    }
    // A short read leaves the tail uninitialised, so the whole buffer is
    // refilled rather than mixing urandom bytes with stack contents.
    spdlog::warn("[Random] /dev/urandom unavailable, using std::random_device");
    std::random_device rd;
    for (size_t i = 0; i < count; i += 4) {
        uint32_t val = rd();
        for (size_t b = 0; b < 4 && i + b < count; ++b) {
            out[i + b] = static_cast<unsigned char>((val >> (8 * b)) & 0xFF);
        }
    }
}

} // namespace helix
