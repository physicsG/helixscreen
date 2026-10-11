// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file thumbnail_rules.h
 * @brief The thumbnail decisions every backend shares
 *
 * The desktop disk cache, the firmware card grid and the firmware active-print
 * path each deliver thumbnails their own way; they ask these functions for the
 * decision. Exception-free and pool-free, so the firmware compiles them.
 */

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

namespace helix {

struct ThumbnailTarget;

/// The largest side any thumbnail decode accepts (a 4K source).
inline constexpr int THUMBNAIL_MAX_SOURCE_DIMENSION = 4096;

/// What a thumbnail byte stream actually is, read from its magic bytes.
enum class ImageFormat : uint8_t { Unknown, Png, Jpeg, Qoi };

[[nodiscard]] ImageFormat sniff_image_format(const uint8_t* data, size_t size);

[[nodiscard]] inline ImageFormat sniff_image_format(std::string_view bytes) {
    return sniff_image_format(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
}

[[nodiscard]] inline ImageFormat sniff_image_format(const std::vector<uint8_t>& bytes) {
    return sniff_image_format(bytes.data(), bytes.size());
}

/// A PNG that ends in IEND, or a JPEG that ends in EOI. Decoders are fragile on
/// a stream cut mid-way (a partial download, a 100 KB gcode-header boundary),
/// so nothing is decoded that fails this.
[[nodiscard]] bool is_complete_image(const std::vector<uint8_t>& bytes);

/**
 * @brief The bytes as a PNG, which is what every file the cache names `.png` must be
 *
 * A PNG passes through; a complete JPEG is re-encoded (desktop only, up to
 * THUMBNAIL_MAX_SOURCE_DIMENSION a side). Empty for anything else, including
 * QOI, and for a JPEG that is cut short or cannot be re-encoded: LVGL picks its decoder by
 * extension, so a JPEG saved as `.png` renders blank.
 */
[[nodiscard]] std::vector<uint8_t> ensure_png(std::vector<uint8_t> bytes);

/// Where a cached thumbnail came from. Two sources may share an id (a gcode
/// path) without sharing a cache entry.
enum class ThumbnailSource : uint8_t {
    Moonraker,    ///< id = thumbnail path from the gcodes root
    LocalFile,    ///< id = local PNG path (mock metadata)
    GcodeExtract, ///< id = gcode path whose header the PNG was extracted from
    Usb,          ///< id = gcode path on a USB stick
    Timelapse,    ///< id = timelapse video filename
};

/// The string every artifact of (source, id) is hashed from.
[[nodiscard]] std::string thumbnail_cache_id(ThumbnailSource source, const std::string& id);

/// Hash stem shared by a cache id's PNG and all its pre-scaled variants. The
/// connected printer is part of it: a same-named file on another printer is a
/// different file.
[[nodiscard]] std::string thumbnail_hash(const std::string& cache_id);

/// "{hash}.png" with no target, "{hash}_{w}x{h}_ARGB8888.bin" with one. The
/// .bin extension is what LVGL's bin decoder accepts.
[[nodiscard]] std::string thumbnail_file_name(const std::string& hash,
                                              const ThumbnailTarget* target);

/// The cache file name for (source, id), full PNG or pre-scaled to @p target.
[[nodiscard]] std::string thumbnail_key(ThumbnailSource source, const std::string& id,
                                        const ThumbnailTarget* target = nullptr);

/// Whether a file cached at @p cache_mtime still describes a source last
/// modified at @p source_modified. 0 means the source time is unknown, which
/// skips the check.
[[nodiscard]] inline bool is_fresh(time_t cache_mtime, time_t source_modified) {
    return source_modified <= 0 || cache_mtime >= source_modified;
}

} // namespace helix
