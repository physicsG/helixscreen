// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnail_rules.h"

#include "http_request_epoch.h"
#include "thumbnail_processor.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>

#if !defined(HELIX_PLATFORM_ESP32)
#include "lodepng_encode.h"
#include "stb_image.h"

#include <lvgl.h>
#endif

namespace helix {

ImageFormat sniff_image_format(const uint8_t* data, size_t size) {
    static const uint8_t PNG_SIG[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (!data) {
        return ImageFormat::Unknown;
    }
    if (size >= sizeof(PNG_SIG) && std::memcmp(data, PNG_SIG, sizeof(PNG_SIG)) == 0) {
        return ImageFormat::Png;
    }
    if (size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        return ImageFormat::Jpeg;
    }
    if (size >= 4 && std::memcmp(data, "qoif", 4) == 0) {
        return ImageFormat::Qoi;
    }
    return ImageFormat::Unknown;
}

bool is_complete_image(const std::vector<uint8_t>& bytes) {
    static const uint8_t IEND[4] = {'I', 'E', 'N', 'D'};
    static const uint8_t EOI[2] = {0xFF, 0xD9};

    const uint8_t* marker = nullptr;
    size_t marker_size = 0;
    switch (sniff_image_format(bytes)) {
    case ImageFormat::Png:
        marker = IEND;
        marker_size = sizeof(IEND);
        break;
    case ImageFormat::Jpeg:
        marker = EOI;
        marker_size = sizeof(EOI);
        break;
    default:
        return false;
    }

    // The end marker sits in the last few bytes; scanning the final 16
    // tolerates a stray trailing byte or two.
    constexpr size_t window = 16;
    if (bytes.size() < window) {
        return false;
    }
    const uint8_t* tail = bytes.data() + (bytes.size() - window);
    for (size_t i = 0; i + marker_size <= window; ++i) {
        if (std::memcmp(tail + i, marker, marker_size) == 0) {
            return true;
        }
    }
    return false;
}

namespace {

std::vector<uint8_t> jpeg_to_png(const std::vector<uint8_t>& jpeg) {
#if defined(HELIX_PLATFORM_ESP32)
    (void)jpeg;
    return {};
#else
    constexpr int kMaxSide = THUMBNAIL_MAX_SOURCE_DIMENSION;
    const int len = static_cast<int>(jpeg.size());
    int w = 0, h = 0, channels = 0;
    if (!stbi_info_from_memory(jpeg.data(), len, &w, &h, &channels) || w <= 0 || h <= 0 ||
        w > kMaxSide || h > kMaxSide) {
        return {};
    }
    std::unique_ptr<unsigned char, void (*)(void*)> rgba(
        stbi_load_from_memory(jpeg.data(), len, &w, &h, &channels, 4), stbi_image_free);
    if (!rgba) {
        return {};
    }
    unsigned char* encoded = nullptr;
    size_t encoded_size = 0;
    const unsigned err = lodepng_encode32(&encoded, &encoded_size, rgba.get(),
                                          static_cast<unsigned>(w), static_cast<unsigned>(h));
    // lodepng allocates through lv_malloc, so the buffer goes back through lv_free.
    std::unique_ptr<unsigned char, void (*)(void*)> owned(encoded, lv_free);
    if (err != 0 || !encoded) {
        return {};
    }
    return {encoded, encoded + encoded_size};
#endif
}

} // namespace

std::vector<uint8_t> ensure_png(std::vector<uint8_t> bytes) {
    switch (sniff_image_format(bytes)) {
    case ImageFormat::Png:
        return bytes;
    case ImageFormat::Jpeg:
        // stb_image overreads the heap on a stream cut mid-way.
        return is_complete_image(bytes) ? jpeg_to_png(bytes) : std::vector<uint8_t>{};
    default:
        return {};
    }
}

std::string thumbnail_cache_id(ThumbnailSource source, const std::string& id) {
    switch (source) {
    case ThumbnailSource::Moonraker:
        return id;
    case ThumbnailSource::LocalFile:
        return id + "_local";
    case ThumbnailSource::GcodeExtract:
        return id + "_extracted";
    case ThumbnailSource::Usb:
        return "usb:" + id;
    case ThumbnailSource::Timelapse:
        return "tl_" + std::to_string(std::hash<std::string>{}(id));
    }
    return id;
}

std::string thumbnail_hash(const std::string& cache_id) {
    const std::string scoped = std::to_string(http_epoch::printer_key()) + '\n' + cache_id;
    return std::to_string(std::hash<std::string>{}(scoped));
}

std::string thumbnail_file_name(const std::string& hash, const ThumbnailTarget* target) {
    if (!target) {
        return hash + ".png";
    }
    char suffix[48];
    std::snprintf(suffix, sizeof(suffix), "_%dx%d_ARGB8888.bin", target->width, target->height);
    return hash + suffix;
}

std::string thumbnail_key(ThumbnailSource source, const std::string& id,
                          const ThumbnailTarget* target) {
    return thumbnail_file_name(thumbnail_hash(thumbnail_cache_id(source, id)), target);
}

} // namespace helix
