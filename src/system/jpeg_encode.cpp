// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "jpeg_encode.h"

#include "remote_screen_blit.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

namespace helix {

void convert_to_rgb888(const uint8_t* src, uint32_t src_stride, int w, int h, int src_bpp,
                       uint8_t* dst) {
    for (int y = 0; y < h; ++y) {
        const uint8_t* s = src + static_cast<size_t>(y) * src_stride;
        uint8_t* d = dst + static_cast<size_t>(y) * static_cast<size_t>(w) * 3;
        if (src_bpp == 4) {
            for (int x = 0; x < w; ++x) {
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                s += 4;
                d += 3;
            }
        } else {
            for (int x = 0; x < w; ++x) {
                rgb565_to_rgb(static_cast<uint16_t>(s[0] | (s[1] << 8)), d[0], d[1], d[2]);
                s += 2;
                d += 3;
            }
        }
    }
}

namespace {

void append_to_vector(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<uint8_t>*>(context);
    const auto* bytes = static_cast<const uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

} // namespace

bool encode_jpeg_rgb888(const uint8_t* rgb, int w, int h, int quality, std::vector<uint8_t>& out) {
    out.clear();
    if (rgb == nullptr || w <= 0 || h <= 0) {
        return false;
    }
    if (quality < 1) {
        quality = 1;
    } else if (quality > 100) {
        quality = 100;
    }
    if (stbi_write_jpg_to_func(append_to_vector, &out, w, h, 3, rgb, quality) == 0) {
        out.clear();
        return false;
    }
    return true;
}

} // namespace helix
