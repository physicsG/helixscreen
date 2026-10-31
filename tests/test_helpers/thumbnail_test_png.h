// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "thumbnail_cache.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

/// Smallest PNG the cache and the processor will both accept: a 10x10 solid
/// square, 75 bytes.
// clang-format off
inline const std::vector<uint8_t> kTinyPng = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x0A,
    0x08, 0x02, 0x00, 0x00, 0x00, 0x02, 0x50, 0x58, 0xEA, 0x00, 0x00, 0x00,
    0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x68, 0x70, 0x50, 0xC0,
    0x83, 0x18, 0x46, 0xA5, 0xB1, 0x21, 0x00, 0x24, 0x51, 0x57, 0x81, 0xF7,
    0xEC, 0xA3, 0x23, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
    0x42, 0x60, 0x82};
// clang-format on

/// Drop a raw PNG into the cache's PNG slot for `key` WITHOUT a pre-scaled
/// .bin. That is the one cache state that sends fetch_optimized down its
/// "PNG is cached, queue the pre-scale" branch, which resolves on the processor
/// pool and delivers through the UpdateQueue — i.e. asynchronously, which is
/// the whole point. A direct write rather than save_raw_png(), which checks the
/// PNG magic, so a test can also plant @p bytes that cannot be decoded.
inline void plant_cached_png(const ThumbnailCache& cache, const std::string& key,
                             const std::vector<uint8_t>& bytes = kTinyPng) {
    const std::string path = cache.get_cache_path(key);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    REQUIRE(std::filesystem::exists(path));
}
