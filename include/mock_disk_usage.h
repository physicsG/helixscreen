// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace helix::mock {

/// The gcodes disk the mocks report, in MB. HELIX_MOCK_DISK_FREE_MB moves the free
/// figure so the low-space status can be reached without filling a disk.
struct DiskMb {
    uint64_t total = 29 * 1024;
    uint64_t free = 18 * 1024;
};

inline DiskMb gcodes_disk_mb() {
    DiskMb disk;
    if (const char* env = std::getenv("HELIX_MOCK_DISK_FREE_MB")) {
        char* end = nullptr;
        const unsigned long long v = std::strtoull(env, &end, 10);
        if (end != env)
            disk.free = std::min<uint64_t>(v, disk.total);
    }
    return disk;
}

} // namespace helix::mock
